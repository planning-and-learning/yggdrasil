/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/incremental/min_distance.hpp"

#include "query_helpers.hpp"
#include "yggdrasil/database/semantics/query_evaluation.hpp"
#include "yggdrasil/database/semantics/relation_repository.hpp"

#include <gtest/gtest.h>
#include <map>
#include <random>
#include <vector>

namespace
{
using Values = ygg::database::DefaultColumnTypes;
using Column = ygg::Index<ygg::database::Column>;
using Relation = ygg::Builder<ygg::database::Relation<Values>>;
using View = ygg::database::BorrowedRelationView<Values>;
using ygg::uint_t;

View borrow(const Relation& relation)
{
    static const auto context = ygg::database::RelationRepositoryFactory<Values> {}.create();
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
        values.push_back(ygg::database::Row<Values>(bytes, columns).get<uint_t>(c));
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
void check_sequence(ygg::database::QueryView<Values> edges,
                    const std::vector<Relation>& maximal,
                    std::vector<Relation>& inputs,
                    std::vector<bool> static_slots,
                    Relation& sources,
                    Relation& targets,
                    size_t steps,
                    Mutate&& mutate)
{
    const auto k = sources.arity();
    const auto plan = ygg::database::DistancePlan<Values>(sources.columns().span(), edges.columns(), targets.columns().span(), Column(99));
    auto blind = ygg::database::incremental::MinDistance<Values>(edges, plan, static_slots);
    auto guided = ygg::database::incremental::MinDistance<Values, HalfDistance>(edges, plan, static_slots);
    auto evaluator = ygg::database::QueryEvaluator<Values>(ygg::database::compile({ edges }));
    evaluator.evaluate(borrow_all(maximal));
    auto supergraph = Relation(evaluator.get_result().columns().span());
    for (size_t i = 0; i < evaluator.get_result().size(); ++i)
        supergraph.insert(evaluator.get_result().row(i));
    auto heuristic = HalfDistance {};
    heuristic.columns = sources.columns().span();
    auto workspace = ygg::database::DistanceWorkspace<Values>(plan);
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
        heuristic.distances = distances_to(supergraph, targets, k);
        ASSERT_EQ(blind.min_distance(borrow(sources), borrow(targets)), expected) << "step " << step;
        ASSERT_EQ(guided.min_distance(borrow(sources), borrow(targets), heuristic), expected) << "step " << step;
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
    auto repository = ygg::tests::qb::repository<Values>();
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
    check_sequence(edges, maximal, inputs, { false }, sources, targets, 300,
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
    auto repository = ygg::tests::qb::repository<Values>();
    namespace qb = ygg::tests::qb;
    // Slots: 0 moves (x, y), 1 free cells (x), 2 blocked pairs (x, y), 3 static steps (x, y), 4 static identity (x, y).
    const auto moves = qb::input(repository, 0, { x, y });
    const auto free = qb::input(repository, 1, { x });
    const auto blocked = qb::input(repository, 2, { x, y });
    const auto steps = qb::input(repository, 3, { x, y });
    const auto identity = qb::input(repository, 4, { x, y });
    const auto first = qb::project(repository,
                                   qb::join(repository, qb::join(repository, qb::rename(repository, moves, { a, a2 }), qb::rename(repository, free, { a2 })),
                                            qb::rename(repository, identity, { b, b2 })),
                                   { a, b, a2, b2 });
    const auto variable_order = std::array { a, b, a2, b2 };
    const auto inputs_of_second = std::array { qb::rename(repository, steps, { b, b2 }), qb::rename(repository, identity, { a, a2 }) };
    const auto second = qb::project(repository, qb::generic_join(repository, std::span<const ygg::database::QueryView<Values>>(inputs_of_second), std::span<const Column>(variable_order)), { a, b, a2, b2 });
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
    check_sequence(edges, maximal, inputs, { false, false, false, true, true }, sources, targets, 300,
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
    const auto plan = ygg::database::DistancePlan<Values>(sources.columns().span(), edges.columns().span(), targets.columns().span(), Column(99));
    auto blind = ygg::database::incremental::MinDistance<Values>(plan);
    auto guided = ygg::database::incremental::MinDistance<Values, HalfDistance>(plan);
    auto complete = Relation({ x, y });
    for (uint_t v = 0; v < n; ++v)
        for (uint_t w = 0; w < n; ++w)
            complete.insert(std::tuple { v, w });
    auto heuristic = HalfDistance {};
    heuristic.columns = sources.columns().span();
    auto workspace = ygg::database::DistanceWorkspace<Values>(plan);
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

}  // namespace
