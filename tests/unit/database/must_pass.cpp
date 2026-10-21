/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/must_pass.hpp"

#include "query_helpers.hpp"
#include "yggdrasil/database/semantics/relation_repository.hpp"
#include "yggdrasil/database/syntax/row.hpp"

#include <gtest/gtest.h>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <tuple>
#include <vector>

namespace
{
using Column = ygg::Index<ygg::database::Column>;
using Relation = ygg::Builder<ygg::database::Relation>;
using View = ygg::database::BorrowedRelationView;
using ygg::uint_t;
namespace qb = ygg::tests::qb;

View borrow(const Relation& relation)
{
    static const auto context = ygg::database::RelationRepositoryFactory {}.create();
    return { relation, context };
}

const Column key(20), u(0), v(1);

using Pairs = std::set<std::pair<uint_t, uint_t>>;  ///< (key, vertex)

const Column probe(30);

/// All objects 0..objects-1 as probes.
Relation all_probes(uint_t objects)
{
    auto probes = Relation({ probe });
    for (uint_t object = 0; object < objects; ++object)
        probes.insert(std::tuple { object });
    return probes;
}

/// Must-pass objects of keyed edges (key, from..., to...) between keyed sources and targets (key, vertex...).
Pairs must_pass(const Relation& edges, const Relation& sources, const Relation& targets, const Relation& probes)
{
    auto queries = qb::repository();
    const auto query = qb::input(queries, 0, edges.columns().span());
    const auto plan = ygg::database::MustPassPlan(sources.columns().span(), query.columns(), targets.columns().span(), probes.columns().span(), 1);
    auto computation = ygg::database::MustPass(query, plan);
    auto out = Relation(plan.output_columns().span());
    const auto view = borrow(edges);
    computation.must_pass([&](size_t) { return view; }, borrow(sources), borrow(targets), borrow(probes), out);
    auto result = Pairs();
    for (size_t r = 0; r < out.size(); ++r)
        result.emplace(out[r].get<uint_t>(0), out[r].get<uint_t>(1));
    EXPECT_EQ(result.size(), out.size()) << "every must-pass object is reported once";
    return result;
}

Pairs must_pass(const Relation& edges, const Relation& sources, const Relation& targets) { return must_pass(edges, sources, targets, all_probes(10)); }

using Vertex = std::vector<uint_t>;

/// Whether a target of `key` is reachable from one of its sources without visiting a vertex containing `removed`.
bool reachable(const std::vector<std::tuple<uint_t, Vertex, Vertex>>& edges,
               const std::vector<std::pair<uint_t, Vertex>>& sources,
               const std::vector<std::pair<uint_t, Vertex>>& targets,
               uint_t of,
               std::optional<uint_t> removed)
{
    const auto allowed = [&](const Vertex& vertex) { return !removed || std::ranges::find(vertex, *removed) == vertex.end(); };
    auto frontier = std::vector<Vertex>();
    auto seen = std::set<Vertex>();
    for (const auto& [k, s] : sources)
        if (k == of && allowed(s) && seen.insert(s).second)
            frontier.push_back(s);
    while (!frontier.empty())
    {
        const auto vertex = frontier.back();
        frontier.pop_back();
        for (const auto& [k, t] : targets)
            if (k == of && t == vertex)
                return true;
        for (const auto& [k, from, to] : edges)
            if (k == of && from == vertex && allowed(to) && seen.insert(to).second)
                frontier.push_back(to);
    }
    return false;
}

TEST(DatabaseMustPass, ObjectsOnEveryPathPerKey)
{
    // Key 0: a diamond 1 -> {2, 3} -> 4 -> 5; key 1: the same vertex numbers, but 2 -> 4 is the only route.
    auto edges = Relation({ key, u, v });
    for (const auto& [k, from, to] : std::vector<
             std::tuple<uint_t, uint_t, uint_t>> { { 0, 1, 2 }, { 0, 1, 3 }, { 0, 2, 4 }, { 0, 3, 4 }, { 0, 4, 5 }, { 1, 1, 2 }, { 1, 2, 4 }, { 1, 3, 4 } })
        edges.insert(std::tuple { k, from, to });
    auto sources = Relation({ key, u });
    sources.insert(std::tuple { uint_t(0), uint_t(1) });
    sources.insert(std::tuple { uint_t(1), uint_t(1) });
    sources.insert(std::tuple { uint_t(2), uint_t(1) });  // Key 2 has no edges and no targets.
    auto targets = Relation({ key, u });
    targets.insert(std::tuple { uint_t(0), uint_t(5) });
    targets.insert(std::tuple { uint_t(1), uint_t(4) });
    targets.insert(std::tuple { uint_t(3), uint_t(4) });  // Key 3 has no sources.
    EXPECT_EQ(must_pass(edges, sources, targets), (Pairs { { 0, 1 }, { 0, 4 }, { 0, 5 }, { 1, 1 }, { 1, 2 }, { 1, 4 } }));
}

TEST(DatabaseMustPass, SeveralSourcesAndTargets)
{
    // 1 -> 3, 2 -> 3, 3 -> 4, 3 -> 5: from {1, 2} to {4, 5} only 3 must be passed.
    auto edges = Relation({ key, u, v });
    for (const auto& [from, to] : std::vector<std::pair<uint_t, uint_t>> { { 1, 3 }, { 2, 3 }, { 3, 4 }, { 3, 5 } })
        edges.insert(std::tuple { uint_t(0), from, to });
    auto sources = Relation({ key, u });
    auto targets = Relation({ key, u });
    for (const auto vertex : { uint_t(1), uint_t(2) })
        sources.insert(std::tuple { uint_t(0), vertex });
    for (const auto vertex : { uint_t(4), uint_t(5) })
        targets.insert(std::tuple { uint_t(0), vertex });
    EXPECT_EQ(must_pass(edges, sources, targets), (Pairs { { 0, 3 } }));
    // A source that is a target: the empty path passes nothing else.
    targets.insert(std::tuple { uint_t(0), uint_t(1) });
    sources.clear();
    sources.insert(std::tuple { uint_t(0), uint_t(1) });
    EXPECT_EQ(must_pass(edges, sources, targets), (Pairs { { 0, 1 } }));
}

TEST(DatabaseMustPass, UnreachableTargetsHaveNoMustPassVertices)
{
    auto edges = Relation({ key, u, v });
    edges.insert(std::tuple { uint_t(0), uint_t(1), uint_t(2) });
    auto sources = Relation({ key, u });
    sources.insert(std::tuple { uint_t(0), uint_t(1) });
    auto targets = Relation({ key, u });
    targets.insert(std::tuple { uint_t(0), uint_t(3) });
    EXPECT_TRUE(must_pass(edges, sources, targets).empty());
}

class DatabaseMustPassRandom : public testing::TestWithParam<unsigned>
{
};

TEST_P(DatabaseMustPassRandom, MatchesObjectRemoval)
{
    // Random keyed graphs over vertices of one and of two objects, against the definition: x must be passed iff a target
    // is reachable, but not without the vertices containing x. Only probed objects are reported.
    auto rng = std::mt19937(GetParam());
    constexpr auto keys = uint_t(3);
    constexpr auto objects = uint_t(5);
    for (const auto k : { size_t(1), size_t(2) })
        for (size_t round = 0; round < 20; ++round)
        {
            const auto random_vertex = [&]
            {
                auto vertex = Vertex();
                for (size_t c = 0; c < k; ++c)
                    vertex.push_back(uint_t(rng() % (k == 1 ? 9 : objects)));
                return vertex;
            };
            auto vertex_columns = std::vector<Column> { key };
            auto edge_columns = std::vector<Column> { key };
            for (size_t c = 0; c < k; ++c)
            {
                vertex_columns.push_back(Column(ygg::uint_t(c)));
                edge_columns.push_back(Column(ygg::uint_t(c)));
            }
            for (size_t c = 0; c < k; ++c)
                edge_columns.push_back(Column(ygg::uint_t(k + c)));
            auto edge_list = std::vector<std::tuple<uint_t, Vertex, Vertex>>();
            auto source_list = std::vector<std::pair<uint_t, Vertex>>();
            auto target_list = std::vector<std::pair<uint_t, Vertex>>();
            auto edges = Relation(std::span<const Column>(edge_columns));
            auto sources = Relation(std::span<const Column>(vertex_columns));
            auto targets = Relation(std::span<const Column>(vertex_columns));
            const auto row = [](uint_t of, const Vertex& a, const Vertex& b = {})
            {
                auto values = std::vector<uint_t> { of };
                values.insert(values.end(), a.begin(), a.end());
                values.insert(values.end(), b.begin(), b.end());
                return values;
            };
            const auto insert = [](Relation& relation, const std::vector<uint_t>& values)
            {
                auto bytes = std::vector<std::byte>();
                const auto encoded = ygg::database::encode_row<uint_t>(values, bytes);
                if (relation.contains(encoded))
                    return false;
                relation.insert(encoded);
                return true;
            };
            for (uint_t of = 0; of < keys; ++of)
            {
                for (size_t e = 0; e < 16; ++e)
                {
                    const auto from = random_vertex(), to = random_vertex();
                    if (insert(edges, row(of, from, to)))
                        edge_list.emplace_back(of, from, to);
                }
                for (size_t i = 0; i < 1 + rng() % 2; ++i)
                    if (const auto s = random_vertex(); insert(sources, row(of, s)))
                        source_list.emplace_back(of, s);
                for (size_t i = 0; i < 1 + rng() % 2; ++i)
                    if (const auto t = random_vertex(); insert(targets, row(of, t)))
                        target_list.emplace_back(of, t);
            }
            const auto probed = std::set<uint_t> { 0, 2, 3, 4, 5, 7 };
            auto probes = Relation({ probe });
            for (const auto x : probed)
                probes.insert(std::tuple { x });
            auto expected = Pairs();
            for (uint_t of = 0; of < keys; ++of)
                if (reachable(edge_list, source_list, target_list, of, std::nullopt))
                    for (const auto x : probed)
                        if (!reachable(edge_list, source_list, target_list, of, x))
                            expected.emplace(of, x);
            ASSERT_EQ(must_pass(edges, sources, targets, probes), expected) << "k " << k << " round " << round;
        }
}

INSTANTIATE_TEST_SUITE_P(Seeds, DatabaseMustPassRandom, testing::Range(1u, 9u));

}  // namespace
