/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/min_distance.hpp"

#include "query_helpers.hpp"
#include "yggdrasil/database/semantics/query_evaluation.hpp"
#include "yggdrasil/database/semantics/relation_repository.hpp"

#include <array>
#include <cstring>
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
    views.reserve(relations.size());
    for (const auto& relation : relations)
        views.push_back(borrow(relation));
    return views;
}

std::vector<uint_t> decode(std::span<const std::byte> bytes, std::span<const ygg::database::ColumnLayout> columns)
{
    std::vector<uint_t> values;
    values.reserve(columns.size());
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
                    const std::vector<bool>& static_slots,
                    Relation& sources,
                    Relation& targets,
                    size_t steps,
                    Mutate mutate)
{
    const auto k = sources.arity();
    const auto plan = ygg::database::DistancePlan(sources.columns().span(), edges.columns(), targets.columns().span(), Column(99));
    auto blind = ygg::database::MinDistance(edges, plan, static_slots);
    auto guided = ygg::database::MinDistance<HalfDistance>(edges, plan, static_slots);
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
        heuristic.distances = distances_to(supergraph, targets, k);
        ASSERT_EQ(blind.min_distance(input, borrow(sources), borrow(targets)), expected) << "step " << step;
        ASSERT_EQ(guided.min_distance(input, borrow(sources), borrow(targets), heuristic), expected) << "step " << step;
        checked += expected != std::numeric_limits<uint_t>::max();
    }
    EXPECT_GT(checked, steps / 8);
    EXPECT_GT(guided.statistics().tree_hits, 0);
    EXPECT_GT(guided.statistics().backward_vertices, 0);
}

const Column x(0), y(1), a(10), b(11), a2(12), b2(13);

class DatabaseMinDistance : public testing::TestWithParam<unsigned>
{
};

TEST_P(DatabaseMinDistance, SingleColumnEdgesUnderChanges)
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

TEST_P(DatabaseMinDistance, PairEdgesWithJoinsUnionsSelectionsAndDifferences)
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

TEST_P(DatabaseMinDistance, RelationEdgesUnderChanges)
{
    constexpr uint_t n = 12;
    auto edges = Relation({ x, y });
    auto sources = Relation({ x });
    auto targets = Relation({ y });
    const auto plan = ygg::database::DistancePlan(sources.columns().span(), edges.columns().span(), targets.columns().span(), Column(99));
    auto blind = ygg::database::MinDistance(plan);
    auto guided = ygg::database::MinDistance<HalfDistance>(plan);
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
                    edges.erase(edges.find(edge).value());
                else
                    edges.insert(edge);
            }
        const auto expected = ygg::database::min_distance(borrow(sources), borrow(edges), borrow(targets), plan, result, workspace);
        ASSERT_EQ(blind.min_distance_edges(borrow(edges), borrow(sources), borrow(targets)), expected) << "step " << step;
        ASSERT_EQ(guided.min_distance_edges(borrow(edges), borrow(sources), borrow(targets), heuristic), expected) << "step " << step;
    }
    EXPECT_GT(guided.statistics().tree_hits, 0);
}

/// Vertices are uint_t values; edges are adjacency lists.
struct ListGraph
{
    using Bytes = std::array<std::byte, sizeof(uint_t)>;

    std::vector<std::vector<uint_t>> out;
    std::vector<std::vector<uint_t>> in;

    static uint_t id(std::span<const std::byte> vertex)
    {
        auto value = uint_t(0);
        std::memcpy(&value, vertex.data(), sizeof(value));
        return value;
    }
    static Bytes bytes(uint_t value)
    {
        auto result = Bytes {};
        std::memcpy(result.data(), &value, sizeof(value));
        return result;
    }
    template<typename OnVertex>
    static void each(const std::vector<uint_t>& vertices, const OnVertex& on_vertex)
    {
        for (const auto w : vertices)
            if (const auto b = bytes(w); !on_vertex(std::span<const std::byte>(b)))
                return;
    }
    template<typename OnVertex>
    void successors(std::span<const std::byte> vertex, const OnVertex& on_vertex)
    { each(out[id(vertex)], on_vertex); }
    template<typename OnVertex>
    void predecessors(std::span<const std::byte> vertex, const OnVertex& on_vertex)
    { each(in[id(vertex)], on_vertex); }
    bool edge_exists(std::span<const std::byte> from, std::span<const std::byte> to) { return std::ranges::find(out[id(from)], id(to)) != out[id(from)].end(); }
};

struct ListVertices
{
    std::vector<ListGraph::Bytes> rows;

    size_t size() const noexcept { return rows.size(); }
    std::span<const std::byte> row(size_t i) const { return rows[i]; }
};

/// AStar::path() visits a shortest path from a source to a target, whether the search ends at a target or on a stored
/// chain of the backward frontier.
TEST_P(DatabaseMinDistance, AStarPathIsAShortestPath)
{
    auto rng = std::mt19937(GetParam());
    const auto n = uint_t(40);
    auto paths = size_t(0);
    auto chains = size_t(0);  // Paths that end on a stored chain.
    auto heuristic = ygg::database::BlindDistanceHeuristic {};
    auto tree = ygg::database::SearchTree(sizeof(uint_t));
    auto backward = ygg::database::BackwardFrontier {};
    auto storage = ygg::database::AStarStorage(sizeof(uint_t));
    for (size_t trial = 0; trial < 50; ++trial)
    {
        auto graph = ListGraph { std::vector<std::vector<uint_t>>(n), std::vector<std::vector<uint_t>>(n) };
        for (size_t e = 0; e < 70; ++e)
        {
            const auto u = uint_t(rng() % n);
            const auto v = uint_t(rng() % n);
            if (u != v && std::ranges::find(graph.out[u], v) == graph.out[u].end())
            {
                graph.out[u].push_back(v);
                graph.in[v].push_back(u);
            }
        }
        auto sources = ListVertices {};
        auto targets = ygg::database::VertexSet(sizeof(uint_t));
        for (size_t i = 0; i < 2; ++i)
        {
            sources.rows.push_back(ListGraph::bytes(uint_t(rng() % n)));
            targets.insert(std::span<const std::byte>(ListGraph::bytes(uint_t(rng() % n))));
        }
        tree.clear();
        auto ids = std::vector<uint_t>();
        for (size_t i = 0; i < targets.size(); ++i)
            ids.push_back(tree.ensure(targets[uint_t(i)], heuristic));
        backward.start(ids);
        auto search = ygg::database::AStar<ListGraph, ygg::database::BlindDistanceHeuristic>(graph, tree, backward, heuristic, targets, storage, 0);
        const auto distance = search.run(sources);
        if (distance == std::numeric_limits<uint_t>::max())
            continue;
        auto path = std::vector<uint_t>();
        search.path([&](std::span<const std::byte> vertex) { path.push_back(ListGraph::id(vertex)); });
        // Visited: where the search ended back to its source (the only source on the path, as sources have g = 0),
        // then the stored chain from where it ended to a target.
        const auto is_source = [&](uint_t v) { return std::ranges::any_of(sources.rows, [&](const auto& row) { return ListGraph::id(row) == v; }); };
        const auto edge = [&](uint_t u, uint_t v) { return graph.edge_exists(ListGraph::bytes(u), ListGraph::bytes(v)); };
        const auto source = size_t(std::ranges::find_if(path, is_source) - path.begin());
        ASSERT_LT(source, path.size()) << "trial " << trial;
        ASSERT_EQ(path.size(), distance + 1) << "trial " << trial;
        for (size_t i = 0; i < source; ++i)
            EXPECT_TRUE(edge(path[i + 1], path[i])) << "trial " << trial;
        for (size_t i = source + 1; i < path.size(); ++i)
            EXPECT_TRUE(edge(i == source + 1 ? path.front() : path[i - 1], path[i])) << "trial " << trial;
        EXPECT_TRUE(targets.contains(std::span<const std::byte>(ListGraph::bytes(source + 1 < path.size() ? path.back() : path.front()))));
        ++paths;
        chains += source + 1 < path.size();
    }
    EXPECT_GT(paths, 10);
    EXPECT_GT(chains, 0);
}

INSTANTIATE_TEST_SUITE_P(Seeds, DatabaseMinDistance, testing::Range(1u, 9u));

}  // namespace
