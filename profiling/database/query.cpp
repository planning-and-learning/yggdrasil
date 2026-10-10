/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yggdrasil/database/semantics/incremental/query.hpp"

#include "yggdrasil/database/semantics/generic_join.hpp"
#include "yggdrasil/database/optimization/optimization.hpp"
#include "yggdrasil/database/semantics/query_evaluation.hpp"

#include <array>
#include <benchmark/benchmark.h>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

namespace ygg::profiling
{
namespace
{
namespace db = database;
using Relation = Builder<db::Relation<>>;
using Column = Index<db::Column>;

struct Triangle
{
    std::array<Relation, 3> initial { Relation { Column(0), Column(1) }, Relation { Column(1), Column(2) }, Relation { Column(2), Column(0) } };
    std::array<Relation, 3> changed { Relation { Column(0), Column(1) }, Relation { Column(1), Column(2) }, Relation { Column(2), Column(0) } };
    std::array<Relation, 3> added { Relation { Column(0), Column(1) }, Relation { Column(1), Column(2) }, Relation { Column(2), Column(0) } };
    std::array<Relation, 3> removed { Relation { Column(0), Column(1) }, Relation { Column(1), Column(2) }, Relation { Column(2), Column(0) } };
    db::QueryRepository<> repository = db::QueryRepositoryFactory<> {}.create();
    db::QueryBuilder<> builder;
    db::QueryView<> root;

    explicit Triangle(uint_t count) : root(make_query(count)) {}

    db::QueryView<> make_query(uint_t count)
    {
        for (size_t input = 0; input < 3; ++input)
        {
            for (uint_t i = 0; i < count; ++i)
                initial[input].insert(std::tuple { i, i });
            for (uint_t i = 1; i <= count; ++i)
                changed[input].insert(std::tuple { i, i });
            added[input].insert(std::tuple { count, count });
            removed[input].insert(std::tuple { uint_t(0), uint_t(0) });
        }
        return join(join(input(0), input(1)), input(2));
    }

    db::QueryView<> input(size_t slot)
    {
        auto data = db::checkout<db::Query<db::DefaultColumnTypes, db::QueryInputTag>>(builder);
        data->input_slot = slot;
        const auto columns = initial[slot].columns().span();
        data->columns.set(columns.begin(), columns.end());
        return db::insert_query(repository, builder, *data);
    }

    db::QueryView<> join(db::QueryView<> lhs, db::QueryView<> rhs)
    {
        auto data = db::checkout<db::Query<db::DefaultColumnTypes, db::QueryJoinTag>>(builder);
        data->lhs = lhs.get_index();
        data->rhs = rhs.get_index();
        return db::insert_query(repository, builder, *data);
    }

    auto deltas(bool forward) const
    {
        using Change = std::pair<const Relation&, const Relation&>;
        const auto& a = forward ? added : removed;
        const auto& r = forward ? removed : added;
        return std::array { Change { a[0], r[0] }, Change { a[1], r[1] }, Change { a[2], r[2] } };
    }

    db::Statistics<> statistics() const { return db::collect_statistics<db::DefaultColumnTypes>(initial); }
};

bool equal(const Relation& left, const Relation& right)
{
    if (left.size() != right.size() || !std::ranges::equal(left.columns().span(), right.columns().span()))
        return false;
    for (size_t i = 0; i < left.size(); ++i)
        if (!right.contains(left.row(i)))
            return false;
    return true;
}

bool exact_delta(const db::incremental::Delta<>& delta, const Relation& before, const Relation& after)
{
    size_t added = 0, removed = 0;
    for (size_t i = 0; i < after.size(); ++i)
        if (!before.contains(after.row(i)))
        {
            ++added;
            if (!delta.added.contains(after.row(i)))
                return false;
        }
    for (size_t i = 0; i < before.size(); ++i)
        if (!after.contains(before.row(i)))
        {
            ++removed;
            if (!delta.removed.contains(before.row(i)))
                return false;
        }
    return added == delta.added.size() && removed == delta.removed.size();
}

enum class Planning
{
    original,
    structural,
    measured
};

db::QueryPlan<> plan(const Triangle& trace, Planning planning, const db::Statistics<>& statistics)
{
    switch (planning)
    {
        case Planning::original:
            return db::compile({ trace.root });
        case Planning::structural:
            return db::optimize(trace.root);
        case Planning::measured:
            return db::optimize(trace.root, statistics);
    }
    return {};
}

template<Planning Mode>
void optimize_query(benchmark::State& state)
{
    Triangle trace(static_cast<uint_t>(state.range(0)));
    const auto statistics = trace.statistics();
    for (auto _ : state)
        benchmark::DoNotOptimize(plan(trace, Mode, statistics).node_count());
    state.counters["input_rows"] = static_cast<double>(trace.initial[0].size() * 3);
}

template<bool Incremental, Planning Mode>
void query_updates(benchmark::State& state)
{
    Triangle trace(static_cast<uint_t>(state.range(0)));
    const auto plan = ::ygg::profiling::plan(trace, Mode, trace.statistics());
    const auto& initial = trace.initial;
    const auto& changed = trace.changed;
    const auto forward = trace.deltas(true), backward = trace.deltas(false);
    db::QueryEvaluator<> reference(db::compile({ trace.root }));
    reference.evaluate(initial);
    Relation before(reference.get_result().columns().span());
    db::assign(before, reference.get_result());
    reference.evaluate(changed);
    Relation after(reference.get_result().columns().span());
    db::assign(after, reference.get_result());
    auto evaluator = [&]
    {
        if constexpr (Incremental)
            return db::incremental::QueryEvaluator<>(plan);
        else
            return db::QueryEvaluator<>(plan);
    }();
    if constexpr (Incremental)
        evaluator.initialize(initial);
    const auto update = [&](bool next)
    {
        if constexpr (Incremental)
            evaluator.update(next ? forward : backward);
        else
            evaluator.evaluate(next ? changed : initial);
    };
    update(true);
    if (!equal(evaluator.get_result(), after))
    {
        state.SkipWithError("query forward result mismatch");
        return;
    }
    if constexpr (Incremental)
        if (!exact_delta(evaluator.get_delta(), before, after))
        {
            state.SkipWithError("query forward delta mismatch");
            return;
        }
    update(false);
    if (!equal(evaluator.get_result(), before))
    {
        state.SkipWithError("query undo result mismatch");
        return;
    }
    if constexpr (Incremental)
        if (!exact_delta(evaluator.get_delta(), after, before))
        {
            state.SkipWithError("query undo delta mismatch");
            return;
        }
    for (auto _ : state)
    {
        update(true);
        benchmark::DoNotOptimize(std::addressof(evaluator.get_result()));
        benchmark::ClobberMemory();
        update(false);
        benchmark::DoNotOptimize(std::addressof(evaluator.get_result()));
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * 2);
    state.counters["retained_bytes"] = static_cast<double>(evaluator.memory_usage());
    state.counters["output_rows"] = static_cast<double>(evaluator.get_result().size());
    state.counters["delta_rows"] = 6;
}

template<bool Cold>
void generic_join_full(benchmark::State& state)
{
    Triangle trace(static_cast<uint_t>(state.range(0)));
    const auto& input = trace.initial;
    std::vector<std::vector<db::ColumnLayout>> schemas;
    for (const auto& value : trace.initial)
        schemas.emplace_back(value.columns().span().begin(), value.columns().span().end());
    const std::array order { Column(0), Column(1), Column(2) };
    const db::GenericJoinPlan<> plan(schemas, order, order);
    db::GenericJoinWorkspace<> workspace(plan);
    Relation output(plan.output_columns().span());
    db::generic_join<db::DefaultColumnTypes>(input, plan, output, workspace);
    db::QueryEvaluator<> reference(db::compile({ trace.root }));
    reference.evaluate(input);
    if (!equal(output, reference.get_result()))
    {
        state.SkipWithError("generic join disagrees with binary join");
        return;
    }
    for (auto _ : state)
    {
        if constexpr (Cold)
        {
            db::GenericJoinWorkspace<> temporary_workspace(plan);
            Relation temporary_output(plan.output_columns().span());
            db::generic_join<db::DefaultColumnTypes>(input, plan, temporary_output, temporary_workspace);
            benchmark::DoNotOptimize(std::addressof(temporary_output));
        }
        else
        {
            db::generic_join<db::DefaultColumnTypes>(input, plan, output, workspace);
            benchmark::DoNotOptimize(std::addressof(output));
        }
        benchmark::ClobberMemory();
    }
    state.counters["retained_bytes"] = static_cast<double>(workspace.memory_usage() + output.memory_usage());
    state.counters["output_rows"] = static_cast<double>(output.size());
}

[[maybe_unused]] const auto registered = []
{
    const auto configure = [](benchmark::Benchmark* bench) { bench->ArgName("rows")->Arg(64)->Arg(1024)->Unit(benchmark::kMicrosecond); };
    configure(benchmark::RegisterBenchmark("database/query/optimize/structural", optimize_query<Planning::structural>));
    configure(benchmark::RegisterBenchmark("database/query/optimize/measured", optimize_query<Planning::measured>));
    configure(benchmark::RegisterBenchmark("database/query/full/original", query_updates<false, Planning::original>));
    configure(benchmark::RegisterBenchmark("database/query/full/structural", query_updates<false, Planning::structural>));
    configure(benchmark::RegisterBenchmark("database/query/full/measured", query_updates<false, Planning::measured>));
    configure(benchmark::RegisterBenchmark("database/query/incremental/original", query_updates<true, Planning::original>));
    configure(benchmark::RegisterBenchmark("database/query/incremental/structural", query_updates<true, Planning::structural>));
    configure(benchmark::RegisterBenchmark("database/query/incremental/measured", query_updates<true, Planning::measured>));
    configure(benchmark::RegisterBenchmark("database/query/generic_join/cold", generic_join_full<true>));
    configure(benchmark::RegisterBenchmark("database/query/generic_join/warm", generic_join_full<false>));
    return true;
}();
}  // namespace
}  // namespace ygg::profiling
