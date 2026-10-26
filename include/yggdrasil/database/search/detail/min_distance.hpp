/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEARCH_DETAIL_MIN_DISTANCE_HPP_
#define YGG_DATABASE_SEARCH_DETAIL_MIN_DISTANCE_HPP_

#include "yggdrasil/containers/raw_array_set.hpp"
#include "yggdrasil/database/operators/distance/distance.hpp"
#include "yggdrasil/database/query/query.hpp"
#include "yggdrasil/database/relation/relation_view.hpp"
#include "yggdrasil/database/search/concepts.hpp"
#include "yggdrasil/database/search/search_tree.hpp"

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <span>
#include <vector>

namespace ygg::database::detail
{

std::vector<size_t> column_range(size_t first, size_t last);

QueryView<> checked_edges(QueryView<> edges, const DistancePlan& plan);

/// An evaluator of a query of one input, slot 0, with the plan's edge columns. It compiles the query when constructed,
/// so the repository may end afterwards.
template<typename Evaluator>
Evaluator relation_edges(const DistancePlan& plan, bool static_edges)
{
    auto repository = QueryRepositoryFactory {}.create();
    auto builder = QueryBuilder {};
    auto input = checkout<Query<QueryInputTag>>(builder);
    input->input_slot = 0;
    const auto columns = plan.edge_columns().span();
    input->columns.set(columns.begin(), columns.end());
    return Evaluator(insert_query(repository, builder, *input), { static_edges });
}

/// An edge query bound for search: `Evaluator` (a bound::QueryEvaluator or incremental::bound::QueryEvaluator) over
/// edge columns k·u·u' (key columns k, possibly none, then two vertices of `arity` columns), with the bindings of the
/// key and a source vertex (successors), the key and a target vertex (predecessors), and all of them (edge checks).
/// The key is an argument of each call; with_key fixes it for a search, as a model of the search oracle concepts.
template<typename Evaluator>
class EdgeQuery
{
public:
    EdgeQuery(Evaluator edges, std::span<const ColumnLayout> edge_columns, size_t keys, size_t arity) :
        m_key_size(keys == 0 ? 0 : edge_columns[keys].offset),
        m_target_offset(edge_columns[keys + arity].offset),
        m_vertex_size(m_target_offset - m_key_size),
        m_edges(std::move(edges)),
        m_forward(m_edges.prepare(column_range(0, keys + arity))),
        m_backward(m_edges.prepare(key_and_target_positions(keys, arity))),
        m_check(m_edges.prepare(column_range(0, keys + 2 * arity))),
        m_bound(m_key_size + m_vertex_size),
        m_edge(m_key_size + 2 * m_vertex_size)
    {
    }

    Evaluator& evaluator() noexcept { return m_edges; }
    [[nodiscard]] const Evaluator& evaluator() const noexcept { return m_edges; }
    [[nodiscard]] size_t vertex_size() const noexcept { return m_vertex_size; }
    /// Where an edge row's target vertex starts.
    [[nodiscard]] size_t target_offset() const noexcept { return m_target_offset; }

    template<RowPredicate OnVertex>
    void successors(std::span<const std::byte> key, std::span<const std::byte> vertex, const OnVertex& on_vertex)
    {
        m_edges.evaluate(m_forward,
                         bound(key, vertex),
                         [&](std::span<const std::byte> edge) { return on_vertex(edge.subspan(m_target_offset, m_vertex_size)); });
    }
    template<RowPredicate OnVertex>
    void predecessors(std::span<const std::byte> key, std::span<const std::byte> vertex, const OnVertex& on_vertex)
    {
        m_edges.evaluate(m_backward, bound(key, vertex), [&](std::span<const std::byte> edge) { return on_vertex(edge.subspan(m_key_size, m_vertex_size)); });
    }
    bool edge_exists(std::span<const std::byte> key, std::span<const std::byte> from, std::span<const std::byte> to)
    {
        std::ranges::copy(key, m_edge.begin());
        std::ranges::copy(from, m_edge.begin() + static_cast<std::ptrdiff_t>(m_key_size));
        std::ranges::copy(to, m_edge.begin() + static_cast<std::ptrdiff_t>(m_key_size + m_vertex_size));
        auto exists = false;
        m_edges.evaluate(m_check,
                         m_edge,
                         [&](std::span<const std::byte>)
                         {
                             exists = true;
                             return false;
                         });
        return exists;
    }

    /// The edges of one key as a search oracle (SuccessorOracle, PredecessorOracle, EdgeCheck).
    class Keyed
    {
    public:
        Keyed(EdgeQuery& edges, std::span<const std::byte> key) : m_edges(&edges), m_key(key) {}

        template<RowPredicate OnVertex>
        void successors(std::span<const std::byte> vertex, const OnVertex& on_vertex)
        { m_edges->successors(m_key, vertex, on_vertex); }
        template<RowPredicate OnVertex>
        void predecessors(std::span<const std::byte> vertex, const OnVertex& on_vertex)
        { m_edges->predecessors(m_key, vertex, on_vertex); }
        bool edge_exists(std::span<const std::byte> from, std::span<const std::byte> to) { return m_edges->edge_exists(m_key, from, to); }

    private:
        EdgeQuery* m_edges;
        std::span<const std::byte> m_key;
    };

    Keyed with_key(std::span<const std::byte> key) { return Keyed(*this, key); }

private:
    /// The key columns and the target vertex of an edge, the bound columns of a predecessor query.
    static std::vector<size_t> key_and_target_positions(size_t keys, size_t arity)
    {
        auto positions = column_range(0, keys);
        for (const auto position : column_range(keys + arity, keys + 2 * arity))
            positions.push_back(position);
        return positions;
    }

    /// The key and a vertex as one row of bound columns; without a key, the vertex itself.
    std::span<const std::byte> bound(std::span<const std::byte> key, std::span<const std::byte> vertex)
    {
        if (key.empty())
            return vertex;
        std::ranges::copy(key, m_bound.begin());
        std::ranges::copy(vertex, m_bound.begin() + static_cast<std::ptrdiff_t>(m_key_size));
        return m_bound;
    }

    size_t m_key_size;
    size_t m_target_offset;
    size_t m_vertex_size;
    Evaluator m_edges;
    size_t m_forward;
    size_t m_backward;
    size_t m_check;
    std::vector<std::byte> m_bound;  ///< The key and a vertex, kept to avoid allocations.
    std::vector<std::byte> m_edge;   ///< The key and the vertices of the checked edge, kept to avoid allocations.
};

/// The targets of a search, as a vertex set.
class TargetSet
{
public:
    explicit TargetSet(size_t vertex_size) : m_vertices(vertex_size) {}

    template<VertexRange Targets>
    [[nodiscard]] bool same(const Targets& targets) const
    {
        return targets.size() == m_vertices.size()
               && std::ranges::all_of(std::views::iota(static_cast<size_t>(0), targets.size()), [&](size_t i) { return m_vertices.contains(targets.row(i)); });
    }
    template<VertexRange Targets>
    void assign(const Targets& targets)
    {
        m_vertices.clear();
        for (size_t i = 0; i < targets.size(); ++i)
            m_vertices.insert(targets.row(i));
    }
    [[nodiscard]] const VertexSet& vertices() const noexcept { return m_vertices; }

    /// The targets' indices in the tree, stored with their estimates; valid until the next call.
    template<DistanceHeuristic H>
    std::span<const uint_t> ensure_in(SearchTree& tree, const H& heuristic)
    {
        m_ids.clear();
        for (size_t i = 0; i < m_vertices.size(); ++i)
            m_ids.push_back(tree.ensure(m_vertices[static_cast<uint_t>(i)], heuristic));
        return m_ids;
    }

private:
    VertexSet m_vertices;
    std::vector<uint_t> m_ids;
};

}  // namespace ygg::database::detail

#endif
