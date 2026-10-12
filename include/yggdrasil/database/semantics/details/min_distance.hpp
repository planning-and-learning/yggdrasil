/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_DETAILS_MIN_DISTANCE_HPP_
#define YGG_DATABASE_SEMANTICS_DETAILS_MIN_DISTANCE_HPP_

#include "yggdrasil/containers/raw_array_set.hpp"
#include "yggdrasil/database/semantics/distance.hpp"
#include "yggdrasil/database/semantics/search/search_tree.hpp"
#include "yggdrasil/database/syntax/query.hpp"

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <span>
#include <vector>

namespace ygg::database::detail
{

inline std::vector<size_t> column_range(size_t first, size_t last)
{
    auto positions = std::vector<size_t>();
    for (auto c = first; c < last; ++c)
        positions.push_back(c);
    return positions;
}

template<ColumnTypes Values>
QueryView<Values> checked_edges(QueryView<Values> edges, const DistancePlan<Values>& plan)
{
    require_plan_columns(edges.columns(), plan.edge_columns().span());
    return edges;
}

/// An evaluator of a query of one input, slot 0, with the plan's edge columns. It compiles the query when constructed,
/// so the repository may end afterwards.
template<typename Evaluator, ColumnTypes Values>
Evaluator relation_edges(const DistancePlan<Values>& plan, bool static_edges)
{
    auto repository = QueryRepositoryFactory<Values> {}.create();
    auto builder = QueryBuilder<Values> {};
    auto input = checkout<Query<Values, QueryInputTag>>(builder);
    input->input_slot = 0;
    const auto columns = plan.edge_columns().span();
    input->columns.set(columns.begin(), columns.end());
    return Evaluator(insert_query(repository, builder, *input), { static_edges });
}

/// An edge query bound for search: `Evaluator` (a BoundQueryEvaluator or incremental::BoundQueryEvaluator) with the
/// bindings of the source columns (successors), the target columns (predecessors) and both (edge checks), so that it
/// models the search oracle concepts. Counts the edge checks.
template<typename Evaluator, ColumnTypes Values>
class EdgeQuery
{
public:
    EdgeQuery(Evaluator edges, const DistancePlan<Values>& plan) :
        m_vertex_size(plan.tuple_size()),
        m_target_offset(plan.edge_columns()[plan.arity()].offset),
        m_source_columns(plan.source_columns().span().begin(), plan.source_columns().span().end()),
        m_target_columns(plan.target_columns().span().begin(), plan.target_columns().span().end()),
        m_edges(std::move(edges)),
        m_forward(m_edges.prepare(column_range(0, plan.arity()))),
        m_backward(m_edges.prepare(column_range(plan.arity(), 2 * plan.arity()))),
        m_check(m_edges.prepare(column_range(0, 2 * plan.arity())))
    {
    }

    Evaluator& evaluator() noexcept { return m_edges; }
    const Evaluator& evaluator() const noexcept { return m_edges; }
    size_t vertex_size() const noexcept { return m_vertex_size; }
    /// Where an edge row's target vertex starts.
    size_t target_offset() const noexcept { return m_target_offset; }
    size_t edge_checks() const noexcept { return m_edge_checks; }

    template<RelationViewConcept<Values> S, RelationViewConcept<Values> T>
    void require_columns(const S& sources, const T& targets) const
    {
        require_plan_columns(sources.columns().span(), m_source_columns);
        require_plan_columns(targets.columns().span(), m_target_columns);
    }

    template<typename OnVertex>
    void successors(std::span<const std::byte> vertex, OnVertex&& on_vertex)
    {
        m_edges.evaluate(m_forward, vertex, [&](std::span<const std::byte> edge) { return on_vertex(edge.subspan(m_target_offset)); });
    }
    template<typename OnVertex>
    void predecessors(std::span<const std::byte> vertex, OnVertex&& on_vertex)
    {
        m_edges.evaluate(m_backward, vertex, [&](std::span<const std::byte> edge) { return on_vertex(edge.first(m_target_offset)); });
    }
    bool edge_exists(std::span<const std::byte> from, std::span<const std::byte> to)
    {
        ++m_edge_checks;
        auto bytes = std::vector<std::byte>(from.begin(), from.end());
        bytes.insert(bytes.end(), to.begin(), to.end());
        auto exists = false;
        m_edges.evaluate(m_check,
                         bytes,
                         [&](std::span<const std::byte>)
                         {
                             exists = true;
                             return false;
                         });
        return exists;
    }

private:
    size_t m_vertex_size;
    size_t m_target_offset;
    std::vector<ColumnLayout> m_source_columns;
    std::vector<ColumnLayout> m_target_columns;
    Evaluator m_edges;
    size_t m_forward;
    size_t m_backward;
    size_t m_check;
    size_t m_edge_checks = 0;
};

/// The targets of a search, as a vertex set.
class TargetSet
{
public:
    explicit TargetSet(size_t vertex_size) : m_vertices(vertex_size) {}

    template<typename Targets>
    bool same(const Targets& targets) const
    {
        return targets.size() == m_vertices.size()
               && std::ranges::all_of(std::views::iota(size_t(0), targets.size()), [&](size_t i) { return m_vertices.contains(targets.row(i)); });
    }
    template<typename Targets>
    void assign(const Targets& targets)
    {
        m_vertices.clear();
        for (size_t i = 0; i < targets.size(); ++i)
            m_vertices.insert(targets.row(i));
    }
    const RawArraySet<std::byte, 64>& vertices() const noexcept { return m_vertices; }

    /// The targets' indices in the tree, stored with their estimates.
    template<typename H>
    std::vector<uint_t> ensure_in(SearchTree& tree, const H& heuristic) const
    {
        auto ids = std::vector<uint_t>();
        for (size_t i = 0; i < m_vertices.size(); ++i)
            ids.push_back(tree.ensure(m_vertices[uint_t(i)], heuristic.estimate(m_vertices[uint_t(i)])));
        return ids;
    }

private:
    RawArraySet<std::byte, 64> m_vertices;
};

}  // namespace ygg::database::detail

#endif
