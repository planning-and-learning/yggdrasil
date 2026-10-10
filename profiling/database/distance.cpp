/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/distance.hpp"

#include "yggdrasil/database/semantics/incremental/distance.hpp"

#include <benchmark/benchmark.h>
#include <cstddef>
#include <memory>
#include <span>
#include <tuple>
#include <vector>

namespace ygg::profiling
{
namespace
{
using namespace database;
using Values = TypeList<uint_t>;
using ColumnIndex = Index<Column>;
using RelationBuilder = Builder<Relation<Values>>;

Builder<Columns<Values>> columns(size_t arity, uint_t first_label)
{
    Builder<Columns<Values>> result;
    for (size_t i = 0; i < arity; ++i)
        result.push_back<uint_t>(ColumnIndex(first_label + static_cast<uint_t>(i)));
    return result;
}

void append_vertex(std::vector<std::byte>& bytes, size_t arity, uint_t vertex)
{
    for (size_t field = 0; field < arity; ++field)
    {
        const auto offset = bytes.size();
        bytes.resize(offset + ColumnCodec<uint_t>::size);
        ColumnCodec<uint_t>::encode(vertex + 100000 * static_cast<uint_t>(field), std::span(bytes).subspan(offset));
    }
}

void insert_vertex(RelationBuilder& relation, size_t arity, uint_t vertex)
{
    std::vector<std::byte> bytes;
    append_vertex(bytes, arity, vertex);
    relation.insert(std::span<const std::byte>(bytes));
}

void insert_edge(RelationBuilder& relation, size_t arity, uint_t from, uint_t to)
{
    std::vector<std::byte> bytes;
    append_vertex(bytes, arity, from);
    append_vertex(bytes, arity, to);
    relation.insert(std::span<const std::byte>(bytes));
}

enum class TraceKind
{
    shortcuts,
    self_loops,
    witness_tail,
    dense_ties,
};

struct Trace
{
    RelationBuilder sources;
    RelationBuilder edges;
    RelationBuilder changed_edges;
    RelationBuilder targets;
    incremental::Delta<Values> edge_change;
    RelationBuilder no_sources;
    RelationBuilder no_targets;

    Trace(size_t arity, uint_t vertices, uint_t source_count, uint_t changed, TraceKind kind = TraceKind::shortcuts) :
        sources(columns(arity, 1000)),
        edges(columns(2 * arity, 0)),
        changed_edges(edges.columns().span()),
        targets(columns(arity, 2000)),
        edge_change(edges.columns().span()),
        no_sources(sources.columns().span()),
        no_targets(targets.columns().span())
    {
        const auto common_edge = [&](uint_t from, uint_t to)
        {
            insert_edge(edges, arity, from, to);
            insert_edge(changed_edges, arity, from, to);
        };
        for (uint_t source = 0; source < source_count; ++source)
            insert_vertex(sources, arity, source);
        if (kind == TraceKind::witness_tail)
        {
            const auto old_witness = source_count;
            const auto new_witness = source_count + 1;
            const auto first_tail = source_count + 2;
            for (uint_t source = 0; source < source_count; ++source)
            {
                common_edge(source, old_witness);
                common_edge(source, new_witness);
            }
            insert_edge(edges, arity, old_witness, first_tail);
            insert_edge(changed_edges, arity, new_witness, first_tail);
            insert_edge(edge_change.removed, arity, old_witness, first_tail);
            insert_edge(edge_change.added, arity, new_witness, first_tail);
            for (uint_t vertex = first_tail; vertex < vertices; ++vertex)
            {
                if (vertex + 1 < vertices)
                    common_edge(vertex, vertex + 1);
                insert_vertex(targets, arity, vertex);
            }
            return;
        }
        if (kind == TraceKind::dense_ties)
        {
            const auto width = vertices;
            const auto first_parent = source_count;
            const auto moving = first_parent + width;
            const auto first_neighbor = moving + 1;
            for (uint_t source = 0; source < source_count; ++source)
                for (uint_t parent = first_parent; parent < moving; ++parent)
                    common_edge(source, parent);
            common_edge(first_parent, moving);
            for (uint_t neighbor = first_neighbor; neighbor < first_neighbor + width; ++neighbor)
            {
                common_edge(moving, neighbor);
                for (uint_t parent = first_parent; parent < moving; ++parent)
                    common_edge(parent, neighbor);
                insert_vertex(targets, arity, neighbor);
            }
            // Only this vertex changes distance; its dense neighbors gain/lose one tied witness.
            insert_vertex(targets, arity, moving);
            insert_edge(changed_edges, arity, 0, moving);
            insert_edge(edge_change.added, arity, 0, moving);
            return;
        }
        const bool harmless = kind == TraceKind::self_loops;
        for (uint_t vertex = 0; vertex < vertices; ++vertex)
        {
            insert_edge(edges, arity, vertex, (vertex + 1) % vertices);
            insert_edge(edges, arity, vertex, (vertex + 8) % vertices);
            insert_edge(changed_edges, arity, vertex, (vertex + 1) % vertices);
            insert_edge(changed_edges, arity, vertex, (vertex + (!harmless && vertex < changed ? 9 : 8)) % vertices);
            if (vertex % 4 == 0)
                insert_vertex(targets, arity, vertex);
        }
        for (uint_t vertex = 0; vertex < changed; ++vertex)
        {
            if (harmless)
            {
                insert_edge(edge_change.added, arity, vertex, vertex);
                insert_edge(changed_edges, arity, vertex, vertex);
            }
            else
            {
                insert_edge(edge_change.removed, arity, vertex, (vertex + 8) % vertices);
                insert_edge(edge_change.added, arity, vertex, (vertex + 9) % vertices);
            }
        }
    }
};

bool equal_rows(const RelationBuilder& actual, const RelationBuilder& expected)
{
    if (actual.size() != expected.size())
        return false;
    for (size_t i = 0; i < expected.size(); ++i)
        if (!actual.contains(expected[i]))
            return false;
    return true;
}

bool equal_delta(const incremental::Delta<Values>& actual, const RelationBuilder& before, const RelationBuilder& after)
{
    size_t added = 0, removed = 0;
    for (size_t i = 0; i < after.size(); ++i)
        if (!before.contains(after[i]))
        {
            ++added;
            if (!actual.added.contains(after[i]))
                return false;
        }
    for (size_t i = 0; i < before.size(); ++i)
        if (!after.contains(before[i]))
        {
            ++removed;
            if (!actual.removed.contains(before[i]))
                return false;
        }
    return actual.added.size() == added && actual.removed.size() == removed;
}

template<bool Incremental, TraceKind Kind = TraceKind::shortcuts>
void distance_changes(benchmark::State& state)
{
    const auto arity = static_cast<size_t>(state.range(0));
    const auto vertices = static_cast<uint_t>(state.range(1));
    const auto source_count = static_cast<uint_t>(state.range(2));
    const auto changed = static_cast<uint_t>(state.range(3));
    Trace trace(arity, vertices, source_count, changed, Kind);
    const DistancePlan<Values> plan(trace.sources.columns().span(), trace.edges.columns().span(), trace.targets.columns().span(), ColumnIndex(5000));
    DistanceWorkspace<Values> workspace(plan);
    auto evaluation = [&]
    {
        if constexpr (Incremental)
            return incremental::DistanceEvaluator<Values>(plan);
        else
            return RelationBuilder(plan.output_columns().span());
    }();
    if constexpr (Incremental)
        evaluation.initialize(trace.sources, trace.edges, trace.targets);
    const auto update = [&](bool forward)
    {
        if constexpr (Incremental)
            evaluation.update(std::tie(trace.no_sources, trace.no_sources), std::tie(forward ? trace.edge_change.added : trace.edge_change.removed, forward ? trace.edge_change.removed : trace.edge_change.added), std::tie(trace.no_targets, trace.no_targets));
        else
            distance(trace.sources, forward ? trace.changed_edges : trace.edges, trace.targets, plan, evaluation, workspace);
    };
    const auto result = [&]() -> const RelationBuilder&
    {
        if constexpr (Incremental)
            return evaluation.get_result();
        else
            return evaluation;
    };

    // Preparation, exact result/delta checks, and warmup are outside the timed loop.
    auto before = distance<Values>(trace.sources, trace.edges, trace.targets, plan);
    auto after = distance<Values>(trace.sources, trace.changed_edges, trace.targets, plan);
    update(true);
    if (!equal_rows(result(), after))
    {
        state.SkipWithError("distance forward result disagrees with full evaluation");
        return;
    }
    if constexpr (Incremental)
        if (!equal_delta(evaluation.get_delta(), before, after))
        {
            state.SkipWithError("distance forward delta disagrees with set difference");
            return;
        }
    update(false);
    if (!equal_rows(result(), before))
    {
        state.SkipWithError("distance undo result disagrees with full evaluation");
        return;
    }
    if constexpr (Incremental)
        if (!equal_delta(evaluation.get_delta(), after, before))
        {
            state.SkipWithError("distance undo delta disagrees with set difference");
            return;
        }
    for (size_t i = 0; i < 8; ++i)
    {
        update(true);
        update(false);
    }
    for (auto _ : state)
    {
        update(true);
        benchmark::DoNotOptimize(std::addressof(result()));
        benchmark::ClobberMemory();
        update(false);
        benchmark::DoNotOptimize(std::addressof(result()));
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * 2);
    state.counters["output_rows"] = static_cast<double>(result().size());
    state.counters["input_delta_rows"] = static_cast<double>(trace.edge_change.added.size() + trace.edge_change.removed.size());
    state.counters["updates_per_iteration"] = 2;
    state.counters["graph_edges"] = static_cast<double>(trace.edges.size());
    if constexpr (Incremental)
        state.counters["retained_bytes"] = static_cast<double>(evaluation.memory_usage());
    else
        state.counters["retained_bytes"] = static_cast<double>(evaluation.memory_usage() + workspace.memory_usage());
}

template<bool Incremental>
void distance_initialize_warm(benchmark::State& state)
{
    Trace trace(static_cast<size_t>(state.range(0)), static_cast<uint_t>(state.range(1)), static_cast<uint_t>(state.range(2)), 0);
    const DistancePlan<Values> plan(trace.sources.columns().span(), trace.edges.columns().span(), trace.targets.columns().span(), ColumnIndex(5000));
    DistanceWorkspace<Values> workspace(plan);
    auto evaluation = [&]
    {
        if constexpr (Incremental)
            return incremental::DistanceEvaluator<Values>(plan);
        else
            return RelationBuilder(plan.output_columns().span());
    }();
    const auto initialize = [&]
    {
        if constexpr (Incremental)
            evaluation.initialize(trace.sources, trace.edges, trace.targets);
        else
            distance(trace.sources, trace.edges, trace.targets, plan, evaluation, workspace);
    };
    const auto result = [&]() -> const RelationBuilder&
    {
        if constexpr (Incremental)
            return evaluation.get_result();
        else
            return evaluation;
    };
    auto reference = distance<Values>(trace.sources, trace.edges, trace.targets, plan);
    for (size_t i = 0; i < 8; ++i)
        initialize();
    if (!equal_rows(result(), reference))
    {
        state.SkipWithError("distance initialization disagrees with full evaluation");
        return;
    }
    for (auto _ : state)
    {
        initialize();
        benchmark::DoNotOptimize(std::addressof(result()));
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
    state.counters["initializations_per_iteration"] = 1;
    state.counters["output_rows"] = static_cast<double>(result().size());
    state.counters["graph_edges"] = static_cast<double>(trace.edges.size());
    if constexpr (Incremental)
        state.counters["retained_bytes"] = static_cast<double>(evaluation.memory_usage());
    else
        state.counters["retained_bytes"] = static_cast<double>(evaluation.memory_usage() + workspace.memory_usage());
}

[[maybe_unused]] const auto registered = []
{
    const auto configure = [](benchmark::Benchmark* benchmark)
    {
        benchmark->ArgNames({ "arity", "vertices", "sources", "delta" })->Unit(benchmark::kMicrosecond);
        for (const auto arity : { 1, 2, 4, 8 })
        {
            benchmark->Args({ arity, 64, 1, 1 });
            benchmark->Args({ arity, 256, 4, 16 });
            benchmark->Args({ arity, 256, 16, 1 });
        }
    };
    configure(benchmark::RegisterBenchmark("database/distance/full", distance_changes<false>));
    configure(benchmark::RegisterBenchmark("database/distance/incremental", distance_changes<true>));
    const auto harmless = [](benchmark::Benchmark* benchmark)
    { benchmark->ArgNames({ "arity", "vertices", "sources", "delta" })->Unit(benchmark::kMicrosecond)->Args({ 1, 256, 4, 16 })->Args({ 8, 256, 4, 16 }); };
    harmless(benchmark::RegisterBenchmark("database/distance/unchanged/full", distance_changes<false, TraceKind::self_loops>));
    harmless(benchmark::RegisterBenchmark("database/distance/unchanged/incremental", distance_changes<true, TraceKind::self_loops>));
    const auto witness_tail = [](benchmark::Benchmark* benchmark)
    {
        benchmark->ArgNames({ "arity", "vertices", "sources", "delta" })->Unit(benchmark::kMicrosecond);
        for (const auto vertices : { 64, 256, 1024, 4096 })
            benchmark->Args({ 1, vertices, 1, 1 });
    };
    witness_tail(benchmark::RegisterBenchmark("database/distance/witness_tail/full", distance_changes<false, TraceKind::witness_tail>));
    witness_tail(benchmark::RegisterBenchmark("database/distance/witness_tail/incremental", distance_changes<true, TraceKind::witness_tail>));
    const auto dense_ties = [](benchmark::Benchmark* benchmark)
    {
        benchmark->ArgNames({ "arity", "neighbors", "sources", "delta" })->Unit(benchmark::kMicrosecond);
        for (const auto neighbors : { 16, 64, 256 })
            benchmark->Args({ 1, neighbors, 1, 1 });
    };
    dense_ties(benchmark::RegisterBenchmark("database/distance/dense_ties/full", distance_changes<false, TraceKind::dense_ties>));
    dense_ties(benchmark::RegisterBenchmark("database/distance/dense_ties/incremental", distance_changes<true, TraceKind::dense_ties>));
    const auto initialization = [](benchmark::Benchmark* benchmark)
    { benchmark->ArgNames({ "arity", "vertices", "sources" })->Unit(benchmark::kMicrosecond)->Args({ 1, 64, 1 })->Args({ 8, 256, 16 }); };
    initialization(benchmark::RegisterBenchmark("database/distance/initialize_warm/full", distance_initialize_warm<false>));
    initialization(benchmark::RegisterBenchmark("database/distance/initialize_warm/incremental", distance_initialize_warm<true>));
    return true;
}();
}  // namespace
}  // namespace ygg::profiling
