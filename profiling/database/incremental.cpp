/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/relation_builder.hpp"

#ifndef YGG_DATABASE_APPEND_ONLY_PROFILE
#include "yggdrasil/database/semantics/incremental/join.hpp"
#include "yggdrasil/database/semantics/incremental/projection.hpp"
#include "yggdrasil/database/semantics/relation_pool.hpp"
#endif

#include <algorithm>
#include <array>
#include <benchmark/benchmark.h>
#include <chrono>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace ygg::profiling
{
namespace
{
using ColumnIndex = Index<database::Column>;

// Define YGG_DATABASE_APPEND_ONLY_PROFILE to isolate the append/clear workload
// without pulling in the incremental evaluators.
void append_only(benchmark::State& state)
{
    const auto rows = static_cast<size_t>(state.range(0));
    const auto repetitions = static_cast<size_t>(state.range(1));
    Builder<database::Relation<>> relation { ColumnIndex(1), ColumnIndex(2) };
    const auto refill = [&]
    {
        relation.clear();
        for (size_t i = 0; i < rows; ++i)
            relation.insert(std::tuple { static_cast<uint_t>(i / repetitions), static_cast<uint_t>(i / repetitions + rows) });
    };
    refill();
    for (auto _ : state)
    {
        refill();
        benchmark::DoNotOptimize(std::addressof(relation));
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(rows));
    state.counters["retained_bytes"] = static_cast<double>(relation.memory_usage());
    state.counters["output_rows"] = static_cast<double>(relation.size());
}

#ifndef YGG_DATABASE_APPEND_ONLY_PROFILE

struct Transition
{
    database::incremental::Delta<> delta { ColumnIndex(1), ColumnIndex(2) };
    Builder<database::Relation<>> snapshot { ColumnIndex(1), ColumnIndex(2) };
};

struct Trace
{
    Builder<database::Relation<>> initial { ColumnIndex(1), ColumnIndex(2) };
    std::array<Transition, 6> transitions {};
    const size_t rows;
    const size_t changed_rows;
    const size_t witnesses;
    const bool changing_keys;

    Trace(size_t rows, size_t changed_rows, size_t witnesses, bool changing_keys) :
        rows(rows),
        changed_rows(changed_rows),
        witnesses(witnesses),
        changing_keys(changing_keys)
    {
        if (rows == 0 || witnesses == 0 || changed_rows > rows / 3)
            throw std::invalid_argument("Benchmark trace requires positive rows/witnesses and three disjoint change blocks.");
        if (rows > std::numeric_limits<uint_t>::max() / 4)
            throw std::overflow_error("Benchmark trace row count exceeds the tuple value range.");
        for (size_t i = 0; i < rows; ++i)
            initial.insert(std::tuple { static_cast<uint_t>(i / witnesses), static_cast<uint_t>(i) });
        refresh(0);
    }

    void refresh(uint_t generation)
    {
        // The largest generated value is (generation + 4) * rows - 1.
        // Check before multiplication and the conversion to uint_t below.
        if (generation > std::numeric_limits<uint_t>::max() / rows - 4)
            throw std::overflow_error("Benchmark trace generation exceeds the tuple value range.");
        std::vector<std::array<uint_t, 2>> current;
        current.reserve(rows);
        for (size_t i = 0; i < rows; ++i)
            current.push_back({ static_cast<uint_t>(i / witnesses), static_cast<uint_t>(i) });
        size_t position = 0;
        const auto replace = [&](size_t begin, uint_t phase)
        {
            auto& step = transitions[position++];
            step.delta.clear();
            step.snapshot.clear();
            const auto offset = phase == 0 ? uint_t { 0 } : static_cast<uint_t>((generation + phase) * rows);
            for (size_t i = begin; i < begin + changed_rows; ++i)
            {
                step.delta.removed.insert(std::tuple { current[i][0], current[i][1] });
                current[i] = { static_cast<uint_t>(i / witnesses + (changing_keys ? offset : 0)), static_cast<uint_t>(i + offset) };
                step.delta.added.insert(std::tuple { current[i][0], current[i][1] });
            }
            for (const auto& row : current)
                step.snapshot.insert(std::tuple { row[0], row[1] });
        };
        // A -> B -> C -> B -> A -> D -> A: two levels, undo, then a sibling.
        replace(0, 1);
        replace(changed_rows, 2);
        replace(changed_rows, 0);
        replace(0, 0);
        replace(2 * changed_rows, 3);
        replace(2 * changed_rows, 0);
    }
};

struct JoinTrace
{
    Trace lhs;
    Trace rhs;
    const size_t rows;
    const std::array<size_t, 6> transitions { 0, 1, 2, 3, 4, 5 };

    JoinTrace(size_t rows, size_t changed_rows, size_t witnesses) :
        lhs(rows, changed_rows, witnesses, true),
        rhs(rows, changed_rows, witnesses, true),
        rows(rows)
    {
        const std::array columns { ColumnIndex(1), ColumnIndex(3) };
        rhs.initial.rename(columns);
        for (auto& transition : rhs.transitions)
        {
            transition.delta.added.rename(columns);
            transition.delta.removed.rename(columns);
            transition.snapshot.rename(columns);
        }
    }

    void refresh(uint_t generation)
    {
        lhs.refresh(generation);
        rhs.refresh(generation);
    }
};

bool equal_rows(const Builder<database::Relation<>>& lhs, const Builder<database::Relation<>>& rhs)
{
    if (lhs.size() != rhs.size())
        return false;
    for (size_t i = 0; i < lhs.size(); ++i)
        if (!rhs.contains(lhs.row(i)))
            return false;
    return true;
}

size_t changed_rows(const Builder<database::Relation<>>& before, const Builder<database::Relation<>>& after)
{
    size_t result = 0;
    for (size_t i = 0; i < before.size(); ++i)
        result += !after.contains(before.row(i));
    for (size_t i = 0; i < after.size(); ++i)
        result += !before.contains(after.row(i));
    return result;
}

bool equal_delta(const database::incremental::Delta<>& delta, const Builder<database::Relation<>>& before, const Builder<database::Relation<>>& after)
{
    size_t added = 0;
    size_t removed = 0;
    for (size_t i = 0; i < before.size(); ++i)
        if (!after.contains(before.row(i)))
        {
            ++removed;
            if (!delta.removed.contains(before.row(i)))
                return false;
        }
    for (size_t i = 0; i < after.size(); ++i)
        if (!before.contains(after.row(i)))
        {
            ++added;
            if (!delta.added.contains(after.row(i)))
                return false;
        }
    return delta.added.size() == added && delta.removed.size() == removed;
}

template<bool Latency, typename TraceType, typename Update, typename Result, typename Memory>
void measure(benchmark::State& state, TraceType& trace, Update update, Result result, Memory memory)
{
    // Run the entire reversible workload before measurement, including all
    // intermediate cardinalities. Every iteration then starts at the same root.
    for (size_t warmup = 0; warmup < 8; ++warmup)
        for (const auto& transition : trace.transitions)
            update(transition);

    auto maximum = std::chrono::steady_clock::duration::zero();
    uint_t generation = 3;
    for (auto _ : state)
    {
        if constexpr (Latency)
        {
            // Novel tuple values expose eventual hash-table churn. Corpus work
            // is excluded equally for full snapshots and incremental batches.
            state.PauseTiming();
            trace.refresh(generation);
            generation += 3;
            if (generation >= std::numeric_limits<uint_t>::max() / trace.rows - 4)
                generation = 3;
            state.ResumeTiming();
        }
        auto elapsed = std::chrono::steady_clock::duration::zero();
        for (const auto& transition : trace.transitions)
        {
            if constexpr (Latency)
            {
                const auto begin = std::chrono::steady_clock::now();
                update(transition);
                benchmark::DoNotOptimize(std::addressof(result()));
                benchmark::ClobberMemory();
                const auto duration = std::chrono::steady_clock::now() - begin;
                maximum = std::max(maximum, duration);
                elapsed += duration;
            }
            else
            {
                update(transition);
                benchmark::DoNotOptimize(std::addressof(result()));
                benchmark::ClobberMemory();
            }
        }
        if constexpr (Latency)
            state.SetIterationTime(std::chrono::duration<double>(elapsed).count());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(trace.transitions.size()));
    state.counters["retained_bytes"] = static_cast<double>(memory());
    state.counters["output_rows"] = static_cast<double>(result().size());
    state.counters["input_delta_rows"] = static_cast<double>(2 * state.range(1));
    state.counters["seconds_per_update"] =
        benchmark::Counter(static_cast<double>(trace.transitions.size()), benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
    if constexpr (Latency)
        state.counters["max_update_ns"] = std::chrono::duration<double, std::nano>(maximum).count();
}

template<bool Incremental, bool Latency, bool ReplaceWitnesses>
void projection(benchmark::State& state)
{
    // Witness replacements change payloads while preserving projected keys.
    Trace trace(static_cast<size_t>(state.range(0)), static_cast<size_t>(state.range(1)), static_cast<size_t>(state.range(2)), !ReplaceWitnesses);
    const database::ProjectionPlan<> plan(trace.initial.columns().span(), { ColumnIndex(1) });
    database::Workspace<> workspace;
    auto evaluation = [&]
    {
        if constexpr (Incremental)
            return database::incremental::ProjectionEvaluator<>(plan);
        else
            return Builder<database::Relation<>>(plan.output_columns().span());
    }();
    if constexpr (Incremental)
        evaluation.initialize(trace.initial, workspace);
    const auto update = [&](const Transition& transition)
    {
        if constexpr (Incremental)
            evaluation.update(transition.delta.change(), workspace);
        else
            database::project(transition.snapshot, plan, evaluation, workspace);
    };
    const auto result = [&]() -> const Builder<database::Relation<>>&
    {
        if constexpr (Incremental)
            return evaluation.get_result();
        else
            return evaluation;
    };
    // Validate every transition outside timing, not just the restored root.
    Builder<database::Relation<>> reference(plan.output_columns().span());
    Builder<database::Relation<>> previous(plan.output_columns().span());
    database::Workspace<> reference_workspace;
    database::project(trace.initial, plan, previous, reference_workspace);
    size_t output_delta_rows = 0;
    for (const auto& transition : trace.transitions)
    {
        update(transition);
        database::project(transition.snapshot, plan, reference, reference_workspace);
        if (!equal_rows(result(), reference))
        {
            state.SkipWithError("projection trace disagrees with full recomputation");
            return;
        }
        if constexpr (Incremental)
            if (!equal_delta(evaluation.get_delta(), previous, reference))
            {
                state.SkipWithError("projection delta disagrees with before/after set difference");
                return;
            }
        output_delta_rows += changed_rows(previous, reference);
        std::swap(previous, reference);
    }
    state.counters["mean_output_delta_rows"] = static_cast<double>(output_delta_rows) / trace.transitions.size();
    measure<Latency>(state, trace, update, result, [&] { return evaluation.memory_usage() + workspace.row.capacity(); });
}

template<bool Incremental, bool Latency>
void fixed_input_join(benchmark::State& state)
{
    const auto rows = static_cast<size_t>(state.range(0));
    const auto fanout = static_cast<size_t>(state.range(2));
    Trace trace(rows, static_cast<size_t>(state.range(1)), 1, false);
    database::RelationPool<> pool;
    auto fixed = pool.get_or_allocate({ ColumnIndex(1), ColumnIndex(3) });
    for (size_t key = 0; key < rows; ++key)
        for (size_t match = 0; match < fanout; ++match)
            fixed->insert(std::tuple { static_cast<uint_t>(key), static_cast<uint_t>(match) });
    const database::JoinPlan<> plan(trace.initial.columns().span(), fixed->columns().span());
    const Builder<database::Relation<>> unchanged(fixed->columns().span());
    database::Workspace<> workspace;
    auto evaluation = [&]
    {
        if constexpr (Incremental)
            return database::incremental::JoinEvaluator<>(plan);
        else
            return Builder<database::Relation<>>(plan.output_columns().span());
    }();
    // Full recomputation also retains its index on the unchanged input.
    const database::JoinIndex<> index(*fixed, plan.rhs_keys());
    if constexpr (Incremental)
        evaluation.initialize(trace.initial, *fixed, workspace);
    const auto update = [&](const Transition& transition)
    {
        if constexpr (Incremental)
            evaluation.update(transition.delta.change(), std::tie(unchanged, unchanged), workspace);
        else
            database::join(transition.snapshot, *fixed, plan, index, evaluation, workspace);
    };
    const auto result = [&]() -> const Builder<database::Relation<>>&
    {
        if constexpr (Incremental)
            return evaluation.get_result();
        else
            return evaluation;
    };
    Builder<database::Relation<>> reference(plan.output_columns().span());
    Builder<database::Relation<>> previous(plan.output_columns().span());
    database::Workspace<> reference_workspace;
    database::join(trace.initial, *fixed, plan, index, previous, reference_workspace);
    size_t output_delta_rows = 0;
    for (const auto& transition : trace.transitions)
    {
        update(transition);
        database::join(transition.snapshot, *fixed, plan, index, reference, reference_workspace);
        if (!equal_rows(result(), reference))
        {
            state.SkipWithError("fixed-input join trace disagrees with full recomputation");
            return;
        }
        if constexpr (Incremental)
            if (!equal_delta(evaluation.get_delta(), previous, reference))
            {
                state.SkipWithError("fixed-input-join delta disagrees with before/after set difference");
                return;
            }
        output_delta_rows += changed_rows(previous, reference);
        std::swap(previous, reference);
    }
    state.counters["mean_output_delta_rows"] = static_cast<double>(output_delta_rows) / trace.transitions.size();
    measure<Latency>(state,
                     trace,
                     update,
                     result,
                     [&]
                     {
                         auto bytes = evaluation.memory_usage() + workspace.row.capacity();
                         if constexpr (!Incremental)
                             bytes += index.index().memory_usage();
                         return bytes;
                     });
}

template<bool Incremental, bool Latency>
void changing_join(benchmark::State& state)
{
    JoinTrace trace(static_cast<size_t>(state.range(0)), static_cast<size_t>(state.range(1)), static_cast<size_t>(state.range(2)));
    const database::JoinPlan<> plan(trace.lhs.initial.columns().span(), trace.rhs.initial.columns().span());
    database::Workspace<> workspace;
    auto evaluation = [&]
    {
        if constexpr (Incremental)
            return database::incremental::JoinEvaluator<>(plan);
        else
            return Builder<database::Relation<>>(plan.output_columns().span());
    }();
    if constexpr (Incremental)
        evaluation.initialize(trace.lhs.initial, trace.rhs.initial, workspace);
    const auto update = [&](size_t position)
    {
        const auto& left = trace.lhs.transitions[position];
        const auto& right = trace.rhs.transitions[position];
        if constexpr (Incremental)
            evaluation.update(left.delta.change(), right.delta.change(), workspace);
        else
            // Both inputs change, so full recomputation rebuilds its transient
            // hash index in retained storage using the already prepared plan.
            database::join(left.snapshot, right.snapshot, plan, evaluation, workspace);
    };
    const auto result = [&]() -> const Builder<database::Relation<>>&
    {
        if constexpr (Incremental)
            return evaluation.get_result();
        else
            return evaluation;
    };
    Builder<database::Relation<>> reference(plan.output_columns().span());
    Builder<database::Relation<>> previous(plan.output_columns().span());
    database::Workspace<> reference_workspace;
    database::join(trace.lhs.initial, trace.rhs.initial, plan, previous, reference_workspace);
    size_t output_delta_rows = 0;
    for (const auto position : trace.transitions)
    {
        update(position);
        database::join(trace.lhs.transitions[position].snapshot, trace.rhs.transitions[position].snapshot, plan, reference, reference_workspace);
        if (!equal_rows(result(), reference))
        {
            state.SkipWithError("changing join trace disagrees with full recomputation");
            return;
        }
        if constexpr (Incremental)
            if (!equal_delta(evaluation.get_delta(), previous, reference))
            {
                state.SkipWithError("changing join delta disagrees with before/after set difference");
                return;
            }
        output_delta_rows += changed_rows(previous, reference);
        std::swap(previous, reference);
    }
    state.counters["mean_output_delta_rows"] = static_cast<double>(output_delta_rows) / trace.transitions.size();
    measure<Latency>(state, trace, update, result, [&] { return evaluation.memory_usage() + workspace.row.capacity() + workspace.join_index.memory_usage(); });
    // Each side removes and adds range(1) rows in the same overlapping key groups.
    state.counters["input_delta_rows"] = static_cast<double>(4 * state.range(1));
}

template<bool Latency>
void register_matrix(const std::string& suffix)
{
    const auto configure = [](benchmark::Benchmark* benchmark)
    {
        benchmark->ArgNames({ "rows", "delta", "multiplicity" })->Unit(benchmark::kMicrosecond);
        for (const auto rows : { 1024, 16384 })
            for (const auto delta : { 1, 16, 256 })
                for (const auto multiplicity : { 1, 8 })
                    benchmark->Args({ rows, delta, multiplicity });
        if constexpr (Latency)
            // Bound wall time: refreshing snapshots is deliberately untimed.
            benchmark->UseManualTime()->Iterations(256);
    };
    configure(benchmark::RegisterBenchmark("database/projection/full" + suffix, projection<false, Latency, false>));
    configure(benchmark::RegisterBenchmark("database/projection/incremental" + suffix, projection<true, Latency, false>));
    configure(benchmark::RegisterBenchmark("database/projection_witness_replacement/full" + suffix, projection<false, Latency, true>));
    configure(benchmark::RegisterBenchmark("database/projection_witness_replacement/incremental" + suffix, projection<true, Latency, true>));
    configure(benchmark::RegisterBenchmark("database/fixed_input_join/full" + suffix, fixed_input_join<false, Latency>));
    configure(benchmark::RegisterBenchmark("database/fixed_input_join/incremental" + suffix, fixed_input_join<true, Latency>));
    configure(benchmark::RegisterBenchmark("database/changing_join/full" + suffix, changing_join<false, Latency>));
    configure(benchmark::RegisterBenchmark("database/changing_join/incremental" + suffix, changing_join<true, Latency>));
}
#endif

[[maybe_unused]] const auto registered = []
{
    benchmark::RegisterBenchmark("database/relation/append", append_only)
        ->ArgNames({ "rows", "repetitions" })
        ->Args({ 1024, 1 })
        ->Args({ 1024, 8 })
        ->Args({ 16384, 1 })
        ->Args({ 16384, 8 });
#ifndef YGG_DATABASE_APPEND_ONLY_PROFILE
    register_matrix<false>("");
    // Clock calls live only in these explicit latency cases. The maximum is a
    // diagnostic of occasional slow updates; it is not a stable CI threshold.
    register_matrix<true>("/latency");
#endif
    return true;
}();
}  // namespace
}  // namespace ygg::profiling
