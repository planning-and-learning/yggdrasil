/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "query_helpers.hpp"
#include "yggdrasil/database/semantics/incremental/min_distance.hpp"
#include "yggdrasil/database/semantics/query_evaluation.hpp"
#include "yggdrasil/database/semantics/relation_repository.hpp"

#include <gtest/gtest.h>
#include <map>
#include <random>
#include <vector>

namespace
{
using Column = ygg::Index<ygg::database::Column>;
using Relation = ygg::Builder<ygg::database::Relation>;
using View = ygg::database::BorrowedRelationView;
using ygg::uint_t;

View borrow(const Relation& relation)
{
    static const auto context = ygg::database::RelationRepositoryFactory {}.create();
    return { relation, context };
}

std::vector<View> borrow_all(const std::vector<Relation>& relations)
{
    std::vector<View> views;
    for (const auto& relation : relations)
        views.push_back(borrow(relation));
    return views;
}

std::vector<uint_t> decode(std::span<const std::byte> bytes, std::span<const ygg::database::ColumnLayout> columns)
{
    std::vector<uint_t> values;
    for (size_t c = 0; c < columns.size(); ++c)
        values.push_back(ygg::database::Row(bytes, columns).get<uint_t>(c));
    return values;
}

/// Half the distance to the targets in a supergraph of every state: consistent in all of them.
struct HalfDistance
{
    std::map<std::vector<uint_t>, uint_t> distances;
    std::span<const ygg::database::ColumnLayout> columns;

    uint_t estimate(std::span<const std::byte> vertex) const
    {
        const auto found = distances.find(decode(vertex, columns));
        return found == distances.end() ? std::numeric_limits<uint_t>::max() : found->second / 2;
    }
};

/// Backward breadth-first distances to the targets over materialized edges.
std::map<std::vector<uint_t>, uint_t> distances_to(const Relation& edges, const Relation& targets, size_t k)
{
    std::map<std::vector<uint_t>, uint_t> result;
    std::vector<std::vector<uint_t>> frontier;
    for (size_t i = 0; i < targets.size(); ++i)
    {
        const auto vertex = decode(targets.row(i), targets.columns().span());
        if (result.emplace(vertex, 0).second)
            frontier.push_back(vertex);
    }
    for (uint_t level = 1; !frontier.empty(); ++level)
    {
        std::vector<std::vector<uint_t>> next;
        for (size_t i = 0; i < edges.size(); ++i)
        {
            const auto edge = decode(edges.row(i), edges.columns().span());
            const auto from = std::vector<uint_t>(edge.begin(), edge.begin() + long(k));
            const auto to = std::vector<uint_t>(edge.begin() + long(k), edge.end());
            if (std::ranges::find(frontier, to) != frontier.end() && result.emplace(from, level).second)
                next.push_back(from);
        }
        frontier = std::move(next);
    }
    return result;
}

/// Runs `steps` states; each mutates the inputs and maybe the sources and targets, then compares guided distances
/// (blind and with a consistent heuristic) against ygg::database::min_distance over the fully evaluated edges.
/// `maximal` are inputs whose edges include those of every state.
template<typename Mutate>
void check_sequence(ygg::database::QueryView<> edges,
                    const std::vector<Relation>& maximal,
                    std::vector<Relation>& inputs,
                    std::vector<bool> static_slots,
                    Relation& sources,
                    Relation& targets,
                    size_t steps,
                    Mutate&& mutate)
{
    const auto k = sources.arity();
    const auto plan = ygg::database::DistancePlan(sources.columns().span(), edges.columns(), targets.columns().span(), Column(99));
    auto blind = ygg::database::incremental::MinDistance(edges, plan, static_slots);
    auto guided = ygg::database::incremental::MinDistance<HalfDistance>(edges, plan, static_slots);
    // Fed the exact changes of its inputs (update()) instead of diffing them (bind()).
    auto exact = ygg::database::incremental::MinDistance<HalfDistance>(edges, plan, static_slots);
    auto previous = std::vector<Relation>();
    struct Change
    {
        Relation added;
        Relation removed;
    };
    auto changes = std::vector<Change>();
    auto evaluator = ygg::database::QueryEvaluator(ygg::database::compile({ edges }));
    evaluator.evaluate(borrow_all(maximal));
    auto supergraph = Relation(evaluator.get_result().columns().span());
    for (size_t i = 0; i < evaluator.get_result().size(); ++i)
        supergraph.insert(evaluator.get_result().row(i));
    auto heuristic = HalfDistance {};
    heuristic.columns = sources.columns().span();
    auto workspace = ygg::database::DistanceWorkspace(plan);
    auto result = Relation(plan.output_columns().span());
    auto checked = size_t(0);
    for (size_t step = 0; step < steps; ++step)
    {
        mutate(step);
        const auto views = borrow_all(inputs);
        evaluator.evaluate(views);
        const auto& materialized = evaluator.get_result();
        auto edge_relation = Relation(materialized.columns().span());
        for (size_t i = 0; i < materialized.size(); ++i)
            edge_relation.insert(materialized.row(i));
        const auto expected = ygg::database::min_distance(borrow(sources), borrow(edge_relation), borrow(targets), plan, result, workspace);
        const auto input = [&](size_t slot) { return views[slot]; };
        blind.bind(input);
        guided.bind(input);
        if (previous.empty())
            exact.bind(input);
        else
        {
            auto changed = std::vector<bool>(inputs.size(), false);
            changes.clear();
            for (size_t slot = 0; slot < inputs.size(); ++slot)
            {
                auto& change = changes.emplace_back(Change { Relation(inputs[slot].columns().span()), Relation(inputs[slot].columns().span()) });
                for (size_t i = 0; i < inputs[slot].size(); ++i)
                    if (!previous[slot].contains(inputs[slot].row(i)))
                        change.added.insert(inputs[slot].row(i));
                for (size_t i = 0; i < previous[slot].size(); ++i)
                    if (!inputs[slot].contains(previous[slot].row(i)))
                        change.removed.insert(previous[slot].row(i));
                const auto is_static = slot < static_slots.size() && static_slots[slot];
                changed[slot] = !is_static && (!change.added.empty() || !change.removed.empty());
            }
            exact.update([&](size_t slot) -> const Change& { return changes[slot]; }, changed);
        }
        previous.clear();
        for (const auto& relation : inputs)
        {
            auto& copy = previous.emplace_back(relation.columns().span());
            for (size_t i = 0; i < relation.size(); ++i)
                copy.insert(relation.row(i));
        }
        heuristic.distances = distances_to(supergraph, targets, k);
        ASSERT_EQ(blind.min_distance(borrow(sources), borrow(targets)), expected) << "step " << step;
        ASSERT_EQ(guided.min_distance(borrow(sources), borrow(targets), heuristic), expected) << "step " << step;
        ASSERT_EQ(exact.min_distance(borrow(sources), borrow(targets), heuristic), expected) << "step " << step;
        // Reuse across calls without changes.
        ASSERT_EQ(guided.min_distance(borrow(sources), borrow(targets), heuristic), expected) << "step " << step;
        checked += expected != std::numeric_limits<uint_t>::max();
    }
    EXPECT_GT(checked, steps / 8);
    EXPECT_GT(guided.statistics().tree_hits, 0);
    EXPECT_GT(guided.statistics().repaired, 0);
    EXPECT_GT(guided.statistics().backward_vertices, 0);
}

const Column x(0), y(1), a(10), b(11), a2(12), b2(13);

class DatabaseIncrementalMinDistance : public testing::TestWithParam<unsigned>
{
};

TEST_P(DatabaseIncrementalMinDistance, SingleColumnEdgesUnderChanges)
{
    auto repository = ygg::tests::qb::repository();
    const auto edges = ygg::tests::qb::input(repository, 0, { x, y });
    auto inputs = std::vector<Relation>();
    inputs.emplace_back(std::initializer_list<Column> { x, y });
    auto sources = Relation({ x });
    auto targets = Relation({ y });
    auto random = std::mt19937(GetParam());
    const auto vertex = [&] { return uint_t(random() % 12); };
    auto maximal = std::vector<Relation>();
    maximal.emplace_back(std::initializer_list<Column> { x, y });
    for (uint_t v = 0; v < 12; ++v)
        for (uint_t w = 0; w < 12; ++w)
            maximal[0].insert(std::tuple { v, w });
    check_sequence(edges,
                   maximal,
                   inputs,
                   { false },
                   sources,
                   targets,
                   300,
                   [&](size_t step)
                   {
                       if (step % 25 == 0)
                       {
                           targets.clear();
                           for (int i = 0; i < 2; ++i)
                               targets.insert(std::tuple { vertex() });
                       }
                       sources.clear();
                       sources.insert(std::tuple { vertex() });
                       if (step % 7 == 0)
                           sources.insert(std::tuple { vertex() });
                       for (int i = 0; i < 3; ++i)
                       {
                           const auto edge = std::tuple { vertex(), vertex() };
                           if (inputs[0].contains(edge))
                               inputs[0].erase(*inputs[0].find(edge));
                           else
                               inputs[0].insert(edge);
                       }
                   });
}

TEST_P(DatabaseIncrementalMinDistance, PairEdgesWithJoinsUnionsSelectionsAndDifferences)
{
    auto repository = ygg::tests::qb::repository();
    namespace qb = ygg::tests::qb;
    // Slots: 0 moves (x, y), 1 free cells (x), 2 blocked pairs (x, y), 3 static steps (x, y), 4 static identity (x, y).
    const auto moves = qb::input(repository, 0, { x, y });
    const auto free = qb::input(repository, 1, { x });
    const auto blocked = qb::input(repository, 2, { x, y });
    const auto steps = qb::input(repository, 3, { x, y });
    const auto identity = qb::input(repository, 4, { x, y });
    const auto first = qb::project(repository,
                                   qb::join(repository,
                                            qb::join(repository, qb::rename(repository, moves, { a, a2 }), qb::rename(repository, free, { a2 })),
                                            qb::rename(repository, identity, { b, b2 })),
                                   { a, b, a2, b2 });
    const auto variable_order = std::array { a, b, a2, b2 };
    const auto inputs_of_second = std::array { qb::rename(repository, steps, { b, b2 }), qb::rename(repository, identity, { a, a2 }) };
    const auto second =
        qb::project(repository,
                    qb::generic_join(repository, std::span<const ygg::database::QueryView<>>(inputs_of_second), std::span<const Column>(variable_order)),
                    { a, b, a2, b2 });
    const auto teleport = qb::project(
        repository,
        qb::join(repository, qb::select_value(repository, qb::rename(repository, moves, { a, a2 }), a, uint_t(0)), qb::rename(repository, identity, { b, b2 })),
        { a, b, a2, b2 });
    const auto all = qb::union_(repository, qb::union_(repository, first, second), teleport);
    // Safe-range negation: no edge may enter a blocked pair, nor end on equal coordinates.
    const auto diagonal = qb::select_equal(repository, all, a2, b2);
    const auto edges = qb::difference(repository, qb::antijoin(repository, all, qb::rename(repository, blocked, { a2, b2 })), diagonal);

    auto inputs = std::vector<Relation>();
    for (const auto& columns : std::vector<std::vector<Column>> { { x, y }, { x }, { x, y }, { x, y }, { x, y } })
        inputs.emplace_back(std::span<const Column>(columns));
    constexpr uint_t n = 6;
    for (uint_t v = 0; v < n; ++v)
    {
        inputs[1].insert(std::tuple { v });
        for (uint_t w = 0; w < n; w += 2)
            inputs[0].insert(std::tuple { v, uint_t((v + w + 1) % n) });
        inputs[4].insert(std::tuple { v, v });
        inputs[3].insert(std::tuple { v, uint_t((v + 1) % n) });
    }
    auto sources = Relation({ a, b });
    auto targets = Relation({ a2, b2 });
    auto random = std::mt19937(GetParam());
    const auto value = [&] { return uint_t(random() % n); };
    const auto toggle = [](Relation& relation, auto row)
    {
        if (relation.contains(row))
            relation.erase(*relation.find(row));
        else
            relation.insert(row);
    };
    auto maximal = std::vector<Relation>();
    for (const auto& input : inputs)
        maximal.emplace_back(input.columns().span());
    for (uint_t v = 0; v < n; ++v)
    {
        maximal[1].insert(std::tuple { v });
        for (uint_t w = 0; w < n; ++w)
            maximal[0].insert(std::tuple { v, w });
        maximal[3].insert(std::tuple { v, uint_t((v + 1) % n) });
        maximal[4].insert(std::tuple { v, v });
    }
    check_sequence(edges,
                   maximal,
                   inputs,
                   { false, false, false, true, true },
                   sources,
                   targets,
                   300,
                   [&](size_t step)
                   {
                       if (step % 30 == 0)
                       {
                           targets.clear();
                           targets.insert(std::tuple { value(), value() });
                           targets.insert(std::tuple { value(), value() });
                       }
                       sources.clear();
                       sources.insert(std::tuple { value(), value() });
                       toggle(inputs[0], std::tuple { value(), value() });
                       toggle(inputs[1], std::tuple { value() });
                       if (step % 3 == 0)
                           toggle(inputs[2], std::tuple { value(), value() });
                   });
}

TEST_P(DatabaseIncrementalMinDistance, RelationEdgesUnderChanges)
{
    constexpr uint_t n = 12;
    auto edges = Relation({ x, y });
    auto sources = Relation({ x });
    auto targets = Relation({ y });
    const auto plan = ygg::database::DistancePlan(sources.columns().span(), edges.columns().span(), targets.columns().span(), Column(99));
    auto blind = ygg::database::incremental::MinDistance(plan);
    auto guided = ygg::database::incremental::MinDistance<HalfDistance>(plan);
    auto complete = Relation({ x, y });
    for (uint_t v = 0; v < n; ++v)
        for (uint_t w = 0; w < n; ++w)
            complete.insert(std::tuple { v, w });
    auto heuristic = HalfDistance {};
    heuristic.columns = sources.columns().span();
    auto workspace = ygg::database::DistanceWorkspace(plan);
    auto result = Relation(plan.output_columns().span());
    auto random = std::mt19937(GetParam());
    const auto vertex = [&] { return uint_t(random() % n); };
    for (size_t step = 0; step < 300; ++step)
    {
        if (step % 25 == 0)
        {
            targets.clear();
            targets.insert(std::tuple { vertex() });
            targets.insert(std::tuple { vertex() });
            heuristic.distances = distances_to(complete, targets, 1);
        }
        sources.clear();
        sources.insert(std::tuple { vertex() });
        const auto changed = step % 3 == 0;
        if (changed)
            for (int i = 0; i < 3; ++i)
            {
                const auto edge = std::tuple { vertex(), vertex() };
                if (edges.contains(edge))
                    edges.erase(*edges.find(edge));
                else
                    edges.insert(edge);
            }
        const auto expected = ygg::database::min_distance(borrow(sources), borrow(edges), borrow(targets), plan, result, workspace);
        blind.bind_edges(borrow(edges), changed || step == 0);
        guided.bind_edges(borrow(edges), changed || step == 0);
        ASSERT_EQ(blind.min_distance(borrow(sources), borrow(targets)), expected) << "step " << step;
        ASSERT_EQ(guided.min_distance(borrow(sources), borrow(targets), heuristic), expected) << "step " << step;
    }
    EXPECT_GT(guided.statistics().tree_hits, 0);
}

INSTANTIATE_TEST_SUITE_P(Seeds, DatabaseIncrementalMinDistance, testing::Range(1u, 9u));

TEST(DatabaseIncrementalBoundEvaluation, VersionChangesOnlyWhenRowsCanBeRemoved)
{
    namespace qb = ygg::tests::qb;
    auto repository = qb::repository();
    // E(x, y) without the edges into N(y).
    const auto edges = qb::antijoin(repository, qb::input(repository, 0, { x, y }), qb::rename(repository, qb::input(repository, 1, { x }), { y }));
    auto e = Relation({ x, y });
    auto n = Relation({ x });
    e.insert(std::tuple { uint_t(0), uint_t(1) });
    auto evaluator = ygg::database::incremental::bound::QueryEvaluator(edges);
    evaluator.initialize([&](size_t slot) { return borrow(slot == 0 ? e : n); });
    auto e_added = Relation({ x, y });
    auto e_removed = Relation({ x, y });
    auto n_added = Relation({ x });
    auto n_removed = Relation({ x });
    const auto bumps = [&](size_t slot)
    {
        const auto version = evaluator.version();
        if (slot == 0)
            evaluator.update(0, e_added, e_removed);
        else
            evaluator.update(1, n_added, n_removed);
        e_added.clear();
        e_removed.clear();
        n_added.clear();
        n_removed.clear();
        return evaluator.version() != version;
    };
    e_added.insert(std::tuple { uint_t(1), uint_t(2) });
    EXPECT_FALSE(bumps(0));  // A positive input gains a row: no edge can disappear.
    e_removed.insert(std::tuple { uint_t(0), uint_t(1) });
    EXPECT_TRUE(bumps(0));  // It loses one.
    n_added.insert(std::tuple { uint_t(2) });
    EXPECT_TRUE(bumps(1));  // The negated input gains a row.
    n_removed.insert(std::tuple { uint_t(2) });
    EXPECT_FALSE(bumps(1));  // It loses one.
}

/// The exact change of one input slot, for MinDistance::update().
struct SlotChange
{
    Relation added = Relation({ x, y });
    Relation removed = Relation({ x, y });
};

/// Edges E0(x, y) ∪ E1(x, y) over slots 0 and 1, from source 0 to target 2.
struct TwoSlotEdges
{
    ygg::database::QueryRepository repository = ygg::tests::qb::repository();
    ygg::database::QueryView<> edges =
        ygg::tests::qb::union_(repository, ygg::tests::qb::input(repository, 0, { x, y }), ygg::tests::qb::input(repository, 1, { x, y }));
    Relation sources = Relation({ x });
    Relation targets = Relation({ y });
    ygg::database::DistancePlan plan = ygg::database::DistancePlan(sources.columns().span(), edges.columns(), targets.columns().span(), Column(99));
    std::vector<Relation> inputs;

    TwoSlotEdges()
    {
        sources.insert(std::tuple { uint_t(0) });
        targets.insert(std::tuple { uint_t(2) });
        inputs.emplace_back(std::initializer_list<Column> { x, y });
        inputs.emplace_back(std::initializer_list<Column> { x, y });
        inputs[0].insert(std::tuple { uint_t(0), uint_t(1) });
        inputs[0].insert(std::tuple { uint_t(1), uint_t(2) });
    }
    auto input() const
    {
        return [this](size_t slot) { return borrow(inputs[slot]); };
    }
};

TEST(DatabaseIncrementalMinDistanceContract, InitializeDiscardsTheSearchWithUnchangedTargets)
{
    auto graph = TwoSlotEdges();
    auto changes = std::vector<SlotChange>(2);
    const auto change = [&](size_t slot) -> const SlotChange& { return changes[slot]; };
    for (const auto via_update : { false, true })
    {
        SCOPED_TRACE(via_update);
        auto search = ygg::database::incremental::MinDistance(graph.edges, graph.plan);
        graph.inputs[0].clear();
        graph.inputs[0].insert(std::tuple { uint_t(0), uint_t(1) });
        graph.inputs[0].insert(std::tuple { uint_t(1), uint_t(2) });
        graph.inputs[1].clear();
        if (via_update)
        {
            search.initialize(graph.input());
            changes[1].added.clear();
            changes[1].added.insert(std::tuple { uint_t(1), uint_t(1) });
            search.update(change, { false, true });
        }
        else
            search.bind(graph.input());
        ASSERT_EQ(search.min_distance(borrow(graph.sources), borrow(graph.targets)), 2);
        // A shortcut in a new baseline: the stored path 0 -> 1 -> 2 still exists but is no longer shortest.
        graph.inputs[0].insert(std::tuple { uint_t(0), uint_t(2) });
        search.initialize(graph.input());
        EXPECT_EQ(search.min_distance(borrow(graph.sources), borrow(graph.targets)), 1);
    }
}

TEST(DatabaseIncrementalMinDistanceContract, UpdateRejectsStaticUnboundAndOutOfRangeSlots)
{
    auto graph = TwoSlotEdges();
    auto changes = std::vector<SlotChange>(3);
    changes[1].added.insert(std::tuple { uint_t(1), uint_t(1) });
    changes[2].added.insert(std::tuple { uint_t(1), uint_t(1) });
    const auto change = [&](size_t slot) -> const SlotChange& { return changes[slot]; };

    auto unbound = ygg::database::incremental::MinDistance(graph.edges, graph.plan);
    EXPECT_THROW(unbound.update(change, { false, true }), std::invalid_argument);

    auto with_static = ygg::database::incremental::MinDistance(graph.edges, graph.plan, { false, true });
    with_static.initialize(graph.input());
    EXPECT_THROW(with_static.update(change, { false, true }), std::invalid_argument);

    auto search = ygg::database::incremental::MinDistance(graph.edges, graph.plan);
    search.initialize(graph.input());
    EXPECT_THROW(search.update(change, { false, false, true }), std::invalid_argument);
    EXPECT_EQ(search.min_distance(borrow(graph.sources), borrow(graph.targets)), 2);
}

TEST(DatabaseIncrementalMinDistanceContract, DistancesStayExactAfterAThrowingUpdate)
{
    auto graph = TwoSlotEdges();
    auto search = ygg::database::incremental::MinDistance(graph.edges, graph.plan);
    search.initialize(graph.input());
    ASSERT_EQ(search.min_distance(borrow(graph.sources), borrow(graph.targets)), 2);
    // Slot 0 gains the shortcut 0 -> 2 before slot 1's change throws: its removed row is absent.
    auto changes = std::vector<SlotChange>(2);
    changes[0].added.insert(std::tuple { uint_t(0), uint_t(2) });
    changes[1].removed.insert(std::tuple { uint_t(5), uint_t(5) });
    EXPECT_THROW(search.update([&](size_t slot) -> const SlotChange& { return changes[slot]; }, { true, true }), std::invalid_argument);
    EXPECT_EQ(search.min_distance(borrow(graph.sources), borrow(graph.targets)), 1);
}

TEST(DatabaseIncrementalBoundEvaluation, FailedChangesLeaveTheInputUnchanged)
{
    namespace qb = ygg::tests::qb;
    auto repository = qb::repository();
    auto e = Relation({ x, y });
    e.insert(std::tuple { uint_t(0), uint_t(1) });
    e.insert(std::tuple { uint_t(0), uint_t(2) });
    auto evaluator = ygg::database::incremental::bound::QueryEvaluator(qb::input(repository, 0, { x, y }));
    evaluator.initialize([&](size_t) { return borrow(e); });
    const auto successors = evaluator.prepare({ 0 });
    const auto count = [&]
    {
        auto bytes = std::vector<std::byte>();
        auto rows = size_t(0);
        evaluator.evaluate(successors,
                           ygg::database::encode_row<uint_t>(std::array { uint_t(0) }, bytes),
                           [&](std::span<const std::byte>)
                           {
                               ++rows;
                               return true;
                           });
        return rows;
    };
    ASSERT_EQ(count(), 2);  // Builds the index on x.
    const auto version = evaluator.version();
    auto added = Relation({ x, y });
    auto removed = Relation({ x, y });
    // A present row removed before an absent one.
    removed.insert(std::tuple { uint_t(0), uint_t(1) });
    removed.insert(std::tuple { uint_t(3), uint_t(3) });
    EXPECT_THROW(evaluator.update(0, added, removed), std::invalid_argument);
    // A new row added before a present one.
    removed.clear();
    added.insert(std::tuple { uint_t(0), uint_t(3) });
    added.insert(std::tuple { uint_t(0), uint_t(2) });
    EXPECT_THROW(evaluator.update(0, added, removed), std::invalid_argument);
    EXPECT_EQ(evaluator.version(), version);
    EXPECT_EQ(evaluator.input(0).size(), 2);
    EXPECT_TRUE(evaluator.input(0).contains(std::tuple { uint_t(0), uint_t(1) }));
    EXPECT_FALSE(evaluator.input(0).contains(std::tuple { uint_t(0), uint_t(3) }));
    EXPECT_EQ(count(), 2);
    EXPECT_THROW(evaluator.update(1, added, removed), std::invalid_argument);
}

}  // namespace
