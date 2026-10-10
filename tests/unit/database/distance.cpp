/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/incremental/distance.hpp"

#include "yggdrasil/database/semantics/relation_repository.hpp"

#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <random>
#include <set>
#include <span>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace ygg::tests
{
namespace
{
using namespace database;
using Values = TypeList<uint_t>;
using RelationBuilder = Builder<Relation<Values>>;
using ColumnIndex = Index<Column>;
using Vertices = std::set<uint_t>;
using Edges = std::set<std::pair<uint_t, uint_t>>;
using Results = std::set<std::array<uint_t, 3>>;

template<RelationViewConcept<Values> V>
Results rows(const V& input)
{
    Results result;
    for (size_t i = 0; i < input.size(); ++i)
    {
        const Row<Values> row(input.row(i), input.columns().span());
        result.insert({ row.get<uint_t>(size_t(0)), row.get<uint_t>(size_t(1)), row.get<uint_t>(size_t(2)) });
    }
    return result;
}

void fill(RelationBuilder& input, const Vertices& vertices)
{
    input.clear();
    for (const auto vertex : vertices)
        input.insert(std::tuple { vertex });
}

void fill(RelationBuilder& input, const Edges& edges)
{
    input.clear();
    for (const auto& [from, to] : edges)
        input.insert(std::tuple { from, to });
}

// Independent pair-by-pair BFS, deliberately unrelated to the maintained graph.
Results reference(const Vertices& sources, const Edges& edges, const Vertices& targets, size_t universe)
{
    Results result;
    for (const auto source : sources)
        for (const auto target : targets)
        {
            std::vector<int> distances(universe, -1);
            std::vector<uint_t> queue { source };
            distances[source] = 0;
            for (size_t head = 0; head < queue.size() && distances[target] < 0; ++head)
                for (const auto& [from, to] : edges)
                    if (from == queue[head] && distances[to] < 0)
                    {
                        distances[to] = distances[from] + 1;
                        queue.push_back(to);
                    }
            if (distances[target] >= 0)
                result.insert({ source, target, static_cast<uint_t>(distances[target]) });
        }
    return result;
}

void expect_delta(const incremental::DistanceEvaluator<Values>& evaluator, const Results& before, const Results& after)
{
    Results added, removed;
    for (const auto& row : after)
        if (!before.contains(row))
            added.insert(row);
    for (const auto& row : before)
        if (!after.contains(row))
            removed.insert(row);
    EXPECT_EQ(rows(evaluator.get_result()), after);
    EXPECT_EQ(rows(evaluator.get_delta().added), added);
    EXPECT_EQ(rows(evaluator.get_delta().removed), removed);
}

template<typename Set>
void changes(incremental::Delta<Values>& delta, const Set& before, const Set& after)
{
    Set added, removed;
    for (const auto& value : after)
        if (!before.contains(value))
            added.insert(value);
    for (const auto& value : before)
        if (!after.contains(value))
            removed.insert(value);
    fill(delta.added, added);
    fill(delta.removed, removed);
}
}  // namespace

TEST(YggdrasilTests, DatabaseDistanceIncludesZeroPathsAndKeepsInputTupleLabelsPositional)
{
    RelationBuilder sources { ColumnIndex(11) }, edges { ColumnIndex(1), ColumnIndex(2) }, targets { ColumnIndex(99) };
    fill(sources, Vertices { 0, 5, 8 });
    fill(edges, Edges { { 0, 1 }, { 0, 2 }, { 1, 3 }, { 2, 3 }, { 3, 4 }, { 4, 1 }, { 3, 3 }, { 6, 7 } });
    fill(targets, Vertices { 0, 3, 4, 5, 7, 8 });
    const DistancePlan<Values> plan(sources.columns().span(), edges.columns().span(), targets.columns().span(), ColumnIndex(500));
    EXPECT_EQ(plan.arity(), 1);
    EXPECT_EQ(plan.tuple_size(), sizeof(uint_t));
    ASSERT_EQ(plan.output_columns().size(), 3);
    EXPECT_EQ(plan.output_columns()[0].label, ColumnIndex(1));
    EXPECT_EQ(plan.output_columns()[1].label, ColumnIndex(2));
    EXPECT_EQ(plan.output_columns()[2].label, ColumnIndex(500));
    const Results expected { { 0, 0, 0 }, { 0, 3, 2 }, { 0, 4, 3 }, { 5, 5, 0 }, { 8, 8, 0 } };
    auto result = distance<Values>(sources, edges, targets, plan);
    EXPECT_EQ(rows(result), expected);

    DistanceWorkspace<Values> workspace(plan);
    sources.clear();
    distance(sources, edges, targets, plan, result, workspace);
    EXPECT_TRUE(result.empty());
    fill(sources, Vertices { 5 });
    edges.clear();
    distance(sources, edges, targets, plan, result, workspace);
    EXPECT_EQ(rows(result), (Results { { 5, 5, 0 } }));
    targets.clear();
    distance(sources, edges, targets, plan, result, workspace);
    EXPECT_TRUE(result.empty());
}

TEST(YggdrasilTests, DatabaseDistanceAcceptsInternedViewsWithoutRetainingInputOwners)
{
    RelationBuilder sources { ColumnIndex(10) }, edges { ColumnIndex(1), ColumnIndex(2) }, targets { ColumnIndex(20) };
    fill(sources, Vertices { 1 });
    fill(edges, Edges { { 1, 2 }, { 2, 3 } });
    fill(targets, Vertices { 3 });
    const DistancePlan<Values> plan(sources.columns().span(), edges.columns().span(), targets.columns().span(), ColumnIndex(3));
    RelationRepositoryFactory<Values> factory;
    auto repository = factory.create();
    const auto source_view = insert(repository, sources).first;
    const auto edge_view = insert(repository, edges).first;
    const auto target_view = insert(repository, targets).first;
    auto result = distance<Values>(source_view, edge_view, target_view, plan);
    EXPECT_EQ(rows(result), (Results { { 1, 3, 2 } }));
    incremental::DistanceEvaluator<Values> evaluator(plan);
    evaluator.initialize(source_view, edge_view, target_view);
    repository.clear();
    sources.clear();
    edges.clear();
    targets.clear();
    evaluator.update(std::tie(sources, sources), std::tie(edges, edges), std::tie(targets, targets));
    EXPECT_EQ(rows(evaluator.get_result()), rows(result));
    EXPECT_TRUE(evaluator.get_delta().added.empty());
    EXPECT_TRUE(evaluator.get_delta().removed.empty());
}

TEST(YggdrasilTests, DatabaseDistanceValidatesSchemasBeforeClearingOutput)
{
    RelationBuilder sources { ColumnIndex(10) }, edges { ColumnIndex(1), ColumnIndex(2) }, targets { ColumnIndex(20) };
    fill(sources, Vertices { 1 });
    fill(targets, Vertices { 1 });
    const DistancePlan<Values> plan(sources.columns().span(), edges.columns().span(), targets.columns().span(), ColumnIndex(3));
    EXPECT_THROW((DistancePlan<Values>(sources.columns().span(), sources.columns().span(), targets.columns().span(), ColumnIndex(3))), std::invalid_argument);
    EXPECT_THROW((DistancePlan<Values>(sources.columns().span(), edges.columns().span(), edges.columns().span(), ColumnIndex(3))), std::invalid_argument);
    EXPECT_THROW((DistancePlan<Values>(sources.columns().span(), edges.columns().span(), targets.columns().span(), ColumnIndex(2))), std::invalid_argument);
    Builder<Columns<>> wrong_type;
    wrong_type.push_back<double>(ColumnIndex(20));
    EXPECT_THROW((DistancePlan<>(sources.columns().span(), edges.columns().span(), wrong_type.span(), ColumnIndex(3))), std::invalid_argument);

    DistanceWorkspace<Values> workspace(plan);
    auto output = distance<Values>(sources, edges, targets, plan);
    RelationBuilder wrong { ColumnIndex(77) };
    EXPECT_THROW(distance(wrong, edges, targets, plan, output, workspace), std::invalid_argument);
    EXPECT_THROW(distance(sources, wrong, targets, plan, output, workspace), std::invalid_argument);
    EXPECT_THROW(distance(sources, edges, wrong, plan, output, workspace), std::invalid_argument);
    EXPECT_EQ(rows(output), (Results { { 1, 1, 0 } }));
    EXPECT_THROW(distance(sources, edges, targets, plan, sources, workspace), std::invalid_argument);
    EXPECT_TRUE(sources.contains(std::tuple { uint_t(1) }));

    incremental::DistanceEvaluator<Values> evaluator(plan);
    incremental::Delta<Values> source_delta(sources.columns().span()), edge_delta(edges.columns().span()), target_delta(targets.columns().span());
    EXPECT_THROW((void) evaluator.get_result(), std::logic_error);
    EXPECT_THROW((void) evaluator.get_delta(), std::logic_error);
    EXPECT_THROW(evaluator.update(source_delta.change(), edge_delta.change(), target_delta.change()),
                 std::logic_error);
    evaluator.initialize(sources, edges, targets);
    EXPECT_THROW(evaluator.update(std::tie(wrong, source_delta.removed), edge_delta.change(), target_delta.change()),
                 std::invalid_argument);
    source_delta.added.insert(std::tuple { uint_t(2) });
    source_delta.removed.insert(std::tuple { uint_t(2) });
    EXPECT_THROW(evaluator.update(source_delta.change(), edge_delta.change(), target_delta.change()),
                 std::invalid_argument);
    EXPECT_EQ(rows(evaluator.get_result()), rows(output));
    source_delta.clear();
    evaluator.update(source_delta.change(), edge_delta.change(), target_delta.change());
    EXPECT_EQ(rows(evaluator.get_result()), rows(output));
}

TEST(YggdrasilTests, DatabaseDistanceRejectsChangesThatAreNotActualSetChanges)
{
    RelationBuilder sources { ColumnIndex(10) }, edges { ColumnIndex(1), ColumnIndex(2) }, targets { ColumnIndex(20) };
    fill(sources, Vertices { 1 });
    fill(edges, Edges { { 1, 2 } });
    fill(targets, Vertices { 2 });
    const DistancePlan<Values> plan(sources.columns().span(), edges.columns().span(), targets.columns().span(), ColumnIndex(3));
    incremental::DistanceEvaluator<Values> evaluator(plan);
    incremental::Delta<Values> ds(sources.columns().span()), de(edges.columns().span()), dt(targets.columns().span());
    for (size_t input = 0; input < 3; ++input)
        for (const bool removing : { false, true })
        {
            evaluator.initialize(sources, edges, targets);
            ds.clear();
            de.clear();
            dt.clear();
            if (input == 0)
                (removing ? ds.removed : ds.added).insert(std::tuple { uint_t(removing ? 7 : 1) });
            else if (input == 1)
                (removing ? de.removed : de.added).insert(std::tuple { uint_t(1), uint_t(removing ? 7 : 2) });
            else
                (removing ? dt.removed : dt.added).insert(std::tuple { uint_t(removing ? 7 : 2) });
            // Invalid changes are rejected before mutation, so the previous evaluation remains.
            EXPECT_THROW(evaluator.update(ds.change(), de.change(), dt.change()), std::invalid_argument);
            EXPECT_EQ(rows(evaluator.get_result()), (Results { { 1, 2, 1 } }));
            EXPECT_TRUE(evaluator.get_delta().added.empty() && evaluator.get_delta().removed.empty());
        }
    evaluator.initialize(sources, edges, targets);
    EXPECT_EQ(rows(evaluator.get_result()), (Results { { 1, 2, 1 } }));
}

TEST(YggdrasilTests, DatabaseDistanceRepairsAlternateShortestPathsAndDisconnectsCycles)
{
    RelationBuilder sources { ColumnIndex(10) }, edges { ColumnIndex(1), ColumnIndex(2) }, targets { ColumnIndex(20) };
    fill(sources, Vertices { 0 });
    fill(edges, Edges { { 0, 1 }, { 0, 2 }, { 1, 3 }, { 2, 3 }, { 3, 4 }, { 4, 1 } });
    fill(targets, Vertices { 3, 4 });
    const DistancePlan<Values> plan(sources.columns().span(), edges.columns().span(), targets.columns().span(), ColumnIndex(3));
    incremental::DistanceEvaluator<Values> evaluator(plan);
    evaluator.initialize(sources, edges, targets);
    incremental::Delta<Values> ds(sources.columns().span()), de(edges.columns().span()), dt(targets.columns().span());
    const Results initial { { 0, 3, 2 }, { 0, 4, 3 } };
    EXPECT_EQ(rows(evaluator.get_result()), initial);
    de.removed.insert(std::tuple { uint_t(0), uint_t(1) });
    evaluator.update(ds.change(), de.change(), dt.change());
    expect_delta(evaluator, initial, initial);
    de.clear();
    de.removed.insert(std::tuple { uint_t(0), uint_t(2) });
    evaluator.update(ds.change(), de.change(), dt.change());
    expect_delta(evaluator, initial, {});
    de.clear();
    de.added.insert(std::tuple { uint_t(0), uint_t(4) });
    evaluator.update(ds.change(), de.change(), dt.change());
    const Results repaired { { 0, 3, 3 }, { 0, 4, 1 } };
    expect_delta(evaluator, {}, repaired);
    de.clear();
    de.removed.insert(std::tuple { uint_t(0), uint_t(4) });
    de.added.insert(std::tuple { uint_t(0), uint_t(2) });
    evaluator.update(ds.change(), de.change(), dt.change());
    expect_delta(evaluator, repaired, initial);
}

TEST(YggdrasilTests, DatabaseDistanceMixedReplacementLeavesAnUnchangedChainUnprocessed)
{
    RelationBuilder sources { ColumnIndex(10) }, edges { ColumnIndex(1), ColumnIndex(2) }, targets { ColumnIndex(20) };
    constexpr uint_t last = 130;
    fill(sources, Vertices { 0 });
    Edges edge_set { { 0, 1 }, { 0, 2 }, { 1, 3 } };
    for (uint_t vertex = 3; vertex < last; ++vertex)
        edge_set.emplace(vertex, vertex + 1);
    fill(edges, edge_set);
    fill(targets, Vertices { last });
    const DistancePlan<Values> plan(sources.columns().span(), edges.columns().span(), targets.columns().span(), ColumnIndex(3));
    incremental::DistanceEvaluator<Values> evaluator(plan);
    evaluator.initialize(sources, edges, targets);
    const Results expected { { 0, last, last - 1 } };
    incremental::Delta<Values> ds(sources.columns().span()), de(edges.columns().span()), dt(targets.columns().span());
    fill(de.removed, Edges { { 1, 3 } });
    fill(de.added, Edges { { 2, 3 } });
    for (const bool undo : { false, true })
    {
        SCOPED_TRACE(undo);
        incremental::detail::distance_work = {};
        evaluator.update(ds.change(), std::tie(undo ? de.removed : de.added, undo ? de.added : de.removed), dt.change());
        const auto work = incremental::detail::distance_work;
        expect_delta(evaluator, expected, expected);
        EXPECT_EQ(work.processed_vertices, 0);
        EXPECT_EQ(work.incoming_edges, 0);
        EXPECT_EQ(work.outgoing_edges, 0);
        EXPECT_LE(work.heap_inserts + work.heap_updates + work.heap_erases + work.heap_pops, 8);
    }
}

TEST(YggdrasilTests, DatabaseDistanceEqualSupportsDoNotRescanDenseUnchangedNeighbors)
{
    RelationBuilder sources { ColumnIndex(10) }, edges { ColumnIndex(1), ColumnIndex(2) }, targets { ColumnIndex(20) };
    constexpr uint_t neighbors = 40;
    fill(sources, Vertices { 0 });
    Edges edge_set { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 0, 4 } };
    Vertices target_set { 3 };
    for (uint_t vertex = 5; vertex < 5 + neighbors; ++vertex)
    {
        target_set.insert(vertex);
        edge_set.emplace(3, vertex);
        edge_set.emplace(4, vertex);
        for (uint_t other = 5; other < 5 + neighbors; ++other)
            edge_set.emplace(vertex, other);
    }
    fill(edges, edge_set);
    fill(targets, target_set);
    const DistancePlan<Values> plan(sources.columns().span(), edges.columns().span(), targets.columns().span(), ColumnIndex(3));
    incremental::DistanceEvaluator<Values> evaluator(plan);
    evaluator.initialize(sources, edges, targets);
    const auto before = reference(Vertices { 0 }, edge_set, target_set, 5 + neighbors);
    edge_set.emplace(0, 3);
    const auto after = reference(Vertices { 0 }, edge_set, target_set, 5 + neighbors);
    incremental::Delta<Values> ds(sources.columns().span()), de(edges.columns().span()), dt(targets.columns().span());
    fill(de.added, Edges { { 0, 3 } });
    for (const bool undo : { false, true })
    {
        SCOPED_TRACE(undo);
        incremental::detail::distance_work = {};
        evaluator.update(ds.change(), std::tie(undo ? de.removed : de.added, undo ? de.added : de.removed), dt.change());
        const auto work = incremental::detail::distance_work;
        expect_delta(evaluator, undo ? after : before, undo ? before : after);
        EXPECT_EQ(work.processed_vertices, undo ? 2 : 1);
        EXPECT_LE(work.incoming_edges + work.outgoing_edges, 4 * neighbors + 16);
        EXPECT_LE(work.heap_inserts + work.heap_updates + work.heap_erases + work.heap_pops, 8 * neighbors + 16);
    }
}

TEST(YggdrasilTests, DatabaseDistanceRepairsUnreachableCyclesSelfLoopsAndEdgesIntoTheSource)
{
    RelationBuilder sources { ColumnIndex(10) }, edges { ColumnIndex(1), ColumnIndex(2) }, targets { ColumnIndex(20) };
    const Vertices source_set { 0 }, target_set { 0, 1, 2, 3, 4, 5, 6 };
    Edges edge_set { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 2 }, { 2, 1 }, { 3, 3 }, { 3, 4 }, { 4, 3 }, { 4, 0 } };
    fill(sources, source_set);
    fill(edges, edge_set);
    fill(targets, target_set);
    const DistancePlan<Values> plan(sources.columns().span(), edges.columns().span(), targets.columns().span(), ColumnIndex(3));
    incremental::DistanceEvaluator<Values> evaluator(plan);
    evaluator.initialize(sources, edges, targets);
    incremental::Delta<Values> ds(sources.columns().span()), de(edges.columns().span()), dt(targets.columns().span());
    const std::array<std::pair<Edges, Edges>, 5> batches { {
        { Edges { { 2, 5 } }, Edges { { 0, 1 } } },
        { Edges { { 0, 3 } }, Edges {} },
        { Edges { { 4, 1 } }, Edges {} },
        { Edges { { 5, 0 }, { 2, 2 } }, Edges { { 1, 1 }, { 3, 3 }, { 4, 0 } } },
        { Edges { { 0, 5 }, { 5, 3 } }, Edges { { 0, 3 } } },
    } };
    for (size_t step = 0; step < batches.size(); ++step)
    {
        SCOPED_TRACE(step);
        const auto before = reference(source_set, edge_set, target_set, 7);
        const auto& [added, removed] = batches[step];
        for (const auto& edge : removed)
            edge_set.erase(edge);
        edge_set.insert(added.begin(), added.end());
        fill(de.added, added);
        fill(de.removed, removed);
        evaluator.update(ds.change(), de.change(), dt.change());
        expect_delta(evaluator, before, reference(source_set, edge_set, target_set, 7));
        EXPECT_TRUE(evaluator.get_result().contains(std::tuple { uint_t(0), uint_t(0), uint_t(0) }));
    }
}

TEST(YggdrasilTests, DatabaseDistanceMixedTupleTypesShareCanonicalVertices)
{
    using Mixed = TypeList<uint_t, double, bool>;
    const auto schema = [](uint_t label, size_t tuples)
    {
        Builder<Columns<Mixed>> result;
        for (size_t i = 0; i < tuples; ++i)
        {
            result.push_back<uint_t>(ColumnIndex(label++));
            result.push_back<double>(ColumnIndex(label++));
            result.push_back<bool>(ColumnIndex(label++));
        }
        return result;
    };
    Builder<Relation<Mixed>> sources(schema(11, 1)), edges(schema(1, 2)), targets(schema(21, 1));
    sources.insert(std::tuple { uint_t(7), -0.0, true });
    edges.insert(std::tuple { uint_t(7), +0.0, true, uint_t(8), 1.5, false });
    targets.insert(std::tuple { uint_t(7), +0.0, true });
    targets.insert(std::tuple { uint_t(8), 1.5, false });
    const DistancePlan<Mixed> plan(sources.columns().span(), edges.columns().span(), targets.columns().span(), ColumnIndex(99));
    EXPECT_EQ(plan.arity(), 3);
    EXPECT_EQ(plan.tuple_size(), sizeof(uint_t) + sizeof(double) + 1);
    auto result = distance<Mixed>(sources, edges, targets, plan);
    ASSERT_EQ(result.size(), 2);
    EXPECT_TRUE(result.contains(std::tuple { uint_t(7), 0.0, true, uint_t(7), 0.0, true, uint_t(0) }));
    EXPECT_TRUE(result.contains(std::tuple { uint_t(7), 0.0, true, uint_t(8), 1.5, false, uint_t(1) }));
    incremental::DistanceEvaluator<Mixed> evaluator(plan);
    evaluator.initialize(sources, edges, targets);
    incremental::Delta<Mixed> ds(sources.columns().span()), de(edges.columns().span()), dt(targets.columns().span());
    de.removed.insert(edges[0]);
    evaluator.update(ds.change(), de.change(), dt.change());
    EXPECT_EQ(evaluator.get_result().size(), 1);
    EXPECT_TRUE(evaluator.get_delta().added.empty());
    ASSERT_EQ(evaluator.get_delta().removed.size(), 1);
    EXPECT_TRUE(evaluator.get_delta().removed.contains(std::tuple { uint_t(7), 0.0, true, uint_t(8), 1.5, false, uint_t(1) }));
    evaluator.update(std::tie(ds.removed, ds.added), std::tie(de.removed, de.added), std::tie(dt.removed, dt.added));
    EXPECT_EQ(evaluator.get_result().size(), 2);
    for (size_t i = 0; i < result.size(); ++i)
        EXPECT_TRUE(evaluator.get_result().contains(result[i]));
}

TEST(YggdrasilTests, DatabaseDistanceNullaryTuplesUseTheSingleEmptyVertex)
{
    RelationBuilder sources, edges, targets;
    targets.insert(std::tuple {});
    const DistancePlan<Values> plan(sources.columns().span(), edges.columns().span(), targets.columns().span(), ColumnIndex(3));
    EXPECT_EQ(plan.arity(), 0);
    EXPECT_EQ(plan.tuple_size(), 0);
    auto output = distance<Values>(sources, edges, targets, plan);
    EXPECT_TRUE(output.empty());
    incremental::DistanceEvaluator<Values> evaluator(plan);
    evaluator.initialize(sources, edges, targets);
    RelationBuilder empty, truth;
    truth.insert(std::tuple {});
    evaluator.update(std::tie(truth, empty), std::tie(empty, empty), std::tie(empty, empty));
    ASSERT_EQ(evaluator.get_result().size(), 1);
    EXPECT_TRUE(evaluator.get_result().contains(std::tuple { uint_t(0) }));
    evaluator.update(std::tie(empty, empty), std::tie(truth, empty), std::tie(empty, empty));
    EXPECT_TRUE(evaluator.get_delta().added.empty());
    EXPECT_TRUE(evaluator.get_delta().removed.empty());
    evaluator.update(std::tie(empty, truth), std::tie(empty, truth), std::tie(empty, empty));
    EXPECT_TRUE(evaluator.get_result().empty());
    EXPECT_TRUE(evaluator.get_delta().removed.contains(std::tuple { uint_t(0) }));
    sources.insert(std::tuple {});
    DistanceWorkspace<Values> workspace(plan);
    distance(sources, edges, targets, plan, output, workspace);
    EXPECT_TRUE(output.contains(std::tuple { uint_t(0) }));
}

TEST(YggdrasilTests, DatabaseDistanceGrowsTheSharedDictionaryAndResetsToASmallerUniverse)
{
    RelationBuilder sources { ColumnIndex(10) }, edges { ColumnIndex(1), ColumnIndex(2) }, targets { ColumnIndex(20) };
    fill(sources, Vertices { 1 });
    fill(targets, Vertices { 1 });
    const DistancePlan<Values> plan(sources.columns().span(), edges.columns().span(), targets.columns().span(), ColumnIndex(3));
    incremental::DistanceEvaluator<Values> evaluator(plan);
    evaluator.initialize(sources, edges, targets);
    const Results initial { { 1, 1, 0 } };
    EXPECT_EQ(rows(evaluator.get_result()), initial);
    incremental::Delta<Values> ds(sources.columns().span()), de(edges.columns().span()), dt(targets.columns().span());
    fill(ds.added, Vertices { 33, 65 });
    fill(dt.added, Vertices { 80, 97 });
    Edges expanded_edges;
    for (uint_t vertex = 1; vertex < 80; ++vertex)
        expanded_edges.emplace(vertex, vertex + 1);
    expanded_edges.emplace(80, 97);
    fill(de.added, expanded_edges);
    evaluator.update(ds.change(), de.change(), dt.change());
    const auto expanded = reference(Vertices { 1, 33, 65 }, expanded_edges, Vertices { 1, 80, 97 }, 98);
    ASSERT_EQ(expanded.size(), 7);
    expect_delta(evaluator, initial, expanded);
    evaluator.update(std::tie(ds.removed, ds.added), std::tie(de.removed, de.added), std::tie(dt.removed, dt.added));
    expect_delta(evaluator, expanded, initial);

    fill(sources, Vertices { 2 });
    fill(edges, Edges { { 2, 3 } });
    fill(targets, Vertices { 3 });
    evaluator.initialize(sources, edges, targets);
    const Results smaller { { 2, 3, 1 } };
    EXPECT_EQ(rows(evaluator.get_result()), smaller);
    EXPECT_TRUE(evaluator.get_delta().added.empty());
    EXPECT_TRUE(evaluator.get_delta().removed.empty());
    ds.clear();
    de.clear();
    dt.clear();
    ds.added.insert(std::tuple { uint_t(3) });
    de.added.insert(std::tuple { uint_t(3), uint_t(2) });
    dt.added.insert(std::tuple { uint_t(2) });
    evaluator.update(ds.change(), de.change(), dt.change());
    expect_delta(evaluator, smaller, Results { { 2, 2, 0 }, { 2, 3, 1 }, { 3, 2, 1 }, { 3, 3, 0 } });
}

TEST(YggdrasilTests, DatabaseDistanceMovesRetainedRepairStorageAndResetsAfterward)
{
    RelationBuilder sources { ColumnIndex(10) }, edges { ColumnIndex(1), ColumnIndex(2) }, targets { ColumnIndex(20) };
    fill(sources, Vertices { 0 });
    fill(edges, Edges { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 } });
    fill(targets, Vertices { 3, 4 });
    const DistancePlan<Values> plan(sources.columns().span(), edges.columns().span(), targets.columns().span(), ColumnIndex(3));
    incremental::DistanceEvaluator<Values> evaluator(plan);
    evaluator.initialize(sources, edges, targets);
    incremental::Delta<Values> ds(sources.columns().span()), de(edges.columns().span()), dt(targets.columns().span());
    fill(de.added, Edges { { 0, 3 } });
    const Results initial { { 0, 3, 3 }, { 0, 4, 4 } }, shortcut { { 0, 3, 1 }, { 0, 4, 2 } };
    evaluator.update(ds.change(), de.change(), dt.change());

    incremental::DistanceEvaluator<Values> moved(std::move(evaluator));
    expect_delta(moved, initial, shortcut);
    moved.update(ds.change(), std::tie(de.removed, de.added), dt.change());
    expect_delta(moved, shortcut, initial);

    incremental::DistanceEvaluator<Values> assigned(plan);
    assigned.initialize(sources, edges, targets);
    assigned.update(ds.change(), de.change(), dt.change());
    assigned = std::move(moved);
    expect_delta(assigned, shortcut, initial);
    assigned.update(ds.change(), de.change(), dt.change());
    expect_delta(assigned, initial, shortcut);

    fill(sources, Vertices { 20 });
    fill(edges, Edges { { 20, 21 }, { 21, 21 } });
    fill(targets, Vertices { 20, 21, 22 });
    assigned.initialize(sources, edges, targets);
    const Results reset { { 20, 20, 0 }, { 20, 21, 1 } };
    EXPECT_EQ(rows(assigned.get_result()), reset);
    EXPECT_TRUE(assigned.get_delta().added.empty());
    EXPECT_TRUE(assigned.get_delta().removed.empty());
    fill(de.added, Edges { { 21, 22 } });
    assigned.update(ds.change(), de.change(), dt.change());
    expect_delta(assigned, reset, Results { { 20, 20, 0 }, { 20, 21, 1 }, { 20, 22, 2 } });
}

TEST(YggdrasilTests, DatabaseDistanceRandomBatchesAndUndoMatchIndependentPairwiseBfs)
{
    constexpr uint_t universe = 9;
    const auto toggle = [](auto& set, auto value)
    {
        if (!set.erase(value))
            set.insert(value);
    };
    for (const uint32_t seed : { 73019U, 42U, 0xdeadbeefU, 49157U })
    {
        SCOPED_TRACE(seed);
        std::mt19937 random(seed);
        RelationBuilder sources { ColumnIndex(10) }, edges { ColumnIndex(1), ColumnIndex(2) }, targets { ColumnIndex(20) };
        Vertices source_set { 0, 4 }, target_set { 2, 5, 8 };
        Edges edge_set { { 0, 1 }, { 1, 2 } };
        if (seed % 2 == 0)
            for (uint_t from = 0; from < universe; ++from)
                for (uint_t to = 0; to < universe; ++to)
                    if (random() % 3 != 0)
                        edge_set.emplace(from, to);
        fill(sources, source_set);
        fill(edges, edge_set);
        fill(targets, target_set);
        const DistancePlan<Values> plan(sources.columns().span(), edges.columns().span(), targets.columns().span(), ColumnIndex(3));
        DistanceWorkspace<Values> workspace(plan);
        RelationBuilder full(plan.output_columns().span());
        incremental::DistanceEvaluator<Values> evaluator(plan);
        evaluator.initialize(sources, edges, targets);
        incremental::Delta<Values> ds(sources.columns().span()), de(edges.columns().span()), dt(targets.columns().span());
        for (size_t step = 0; step < 160; ++step)
        {
            SCOPED_TRACE(step);
            const auto old_sources = source_set;
            const auto old_edges = edge_set;
            const auto old_targets = target_set;
            const auto before = reference(old_sources, old_edges, old_targets, universe);
            for (size_t i = 0; i < 1 + step % 7; ++i)
            {
                if (step % 3 == 0)
                    toggle(source_set, uint_t(random() % universe));
                toggle(edge_set, std::pair { uint_t(random() % universe), uint_t(random() % universe) });
                if (step % 2 == 0)
                    toggle(target_set, uint_t(random() % universe));
            }
            changes(ds, old_sources, source_set);
            changes(de, old_edges, edge_set);
            changes(dt, old_targets, target_set);
            evaluator.update(ds.change(), de.change(), dt.change());
            const auto after = reference(source_set, edge_set, target_set, universe);
            expect_delta(evaluator, before, after);
            fill(sources, source_set);
            fill(edges, edge_set);
            fill(targets, target_set);
            distance(sources, edges, targets, plan, full, workspace);
            EXPECT_EQ(rows(full), after);
            if (step % 5 == 0)
            {
                evaluator.update(std::tie(ds.removed, ds.added), std::tie(de.removed, de.added), std::tie(dt.removed, dt.added));
                expect_delta(evaluator, after, before);
                source_set = old_sources;
                edge_set = old_edges;
                target_set = old_targets;
            }
        }
    }
}

}  // namespace ygg::tests
