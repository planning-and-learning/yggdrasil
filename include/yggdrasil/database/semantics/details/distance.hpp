/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_DETAILS_DISTANCE_HPP_
#define YGG_DATABASE_SEMANTICS_DETAILS_DISTANCE_HPP_

#include "yggdrasil/database/semantics/distance.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <stdexcept>

namespace ygg::database
{

namespace detail
{
inline constexpr uint_t distance_infinity = std::numeric_limits<uint_t>::max();

inline DistanceGraph::DistanceGraph(size_t tuple_bytes) : vertices(tuple_bytes) {}

inline uint_t DistanceGraph::insert_vertex(std::span<const std::byte> tuple)
{
    // Reserve the maximum distance as infinity; a simple path has fewer edges than vertices.
    if (vertices.size() >= distance_infinity)
    {
        if (const auto existing = vertices.find(tuple))
            return *existing;
        throw std::length_error("Distance: vertex count exceeds the finite distance range.");
    }
    return vertices.insert(tuple);
}

inline bool DistanceGraph::contains_edge(uint_t from, uint_t to) const
{
    for (const auto target : outgoing.values(from))
        if (target == to)
            return true;
    return false;
}

inline void DistanceGraph::clear()
{
    outgoing.clear();
    vertices.clear();
}

inline size_t DistanceGraph::memory_usage() const noexcept { return vertices.memory_usage() + outgoing.memory_usage(); }

inline void breadth_first_search(const DistanceGraph& graph, uint_t root, std::vector<uint_t>& distances, std::vector<uint_t>& queue)
{
    assert(root < graph.vertices.size());
    distances.assign(graph.vertices.size(), distance_infinity);
    queue.clear();
    distances[root] = 0;
    queue.push_back(root);
    for (size_t position = 0; position < queue.size(); ++position)
    {
        const auto from = queue[position];
        for (const auto to : graph.outgoing.values(from))
            if (distances[to] == distance_infinity)
            {
                distances[to] = distances[from] + 1;
                queue.push_back(to);
            }
    }
}

template<ColumnTypes Values>
    requires ColumnValueFor<uint_t, Values>
Row<Values> make_distance_row(const DistanceGraph& graph, uint_t from, uint_t to, uint_t length, const DistancePlan<Values>& plan, std::vector<std::byte>& row)
{
    assert(length != distance_infinity);
    row.resize(plan.output_columns().row_size());
    const auto tuple_size = plan.tuple_size();
    std::ranges::copy(graph.vertices[from], row.begin());
    std::ranges::copy(graph.vertices[to], row.begin() + tuple_size);
    ColumnCodec<uint_t>::encode(length, std::span(row).subspan(2 * tuple_size));
    return Row<Values>(row, plan.output_columns().span());
}
}  // namespace detail

template<ColumnTypes Values>
DistancePlan<Values>::DistancePlan(std::span<const ColumnLayout> sources,
                                   std::span<const ColumnLayout> edges,
                                   std::span<const ColumnLayout> targets,
                                   Index<Column> distance_column) :
    m_sources(sources),
    m_edges(edges),
    m_targets(targets),
    m_output(edges)
{
    const auto k = arity();
    if (m_targets.size() != k || m_edges.size() % 2 != 0 || m_edges.size() / 2 != k)
        throw std::invalid_argument("Distance: input arities must be k, 2k, k.");
    for (size_t i = 0; i < k; ++i)
        if (m_sources[i].type != m_targets[i].type || m_sources[i].type != m_edges[i].type || m_sources[i].type != m_edges[k + i].type)
            throw std::invalid_argument("Distance: vertex field types must agree by position.");
    if constexpr (ColumnValueFor<uint_t, Values>)
        m_output.template push_back<uint_t>(distance_column);
    else
        throw std::invalid_argument("Distance: uint_t must be a registered column type.");
}

template<ColumnTypes Values>
    requires ColumnValueFor<uint_t, Values>
DistanceWorkspace<Values>::DistanceWorkspace(const DistancePlan<Values>& plan) : graph(plan.tuple_size())
{
}

template<ColumnTypes Values>
    requires ColumnValueFor<uint_t, Values>
size_t DistanceWorkspace<Values>::memory_usage() const noexcept
{
    return graph.memory_usage() + (distances.capacity() + queue.capacity() + targets.capacity()) * sizeof(uint_t) + row.capacity();
}

template<ColumnTypes Values, RelationViewConcept<Values> S, RelationViewConcept<Values> E, RelationViewConcept<Values> T>
    requires ColumnValueFor<uint_t, Values>
void distance(const S& sources,
              const E& edges,
              const T& targets,
              const DistancePlan<Values>& plan,
              Builder<Relation<Values>>& out,
              DistanceWorkspace<Values>& workspace)
{
    detail::require_plan_columns(sources.columns().span(), plan.source_columns().span());
    detail::require_plan_columns(edges.columns().span(), plan.edge_columns().span());
    detail::require_plan_columns(targets.columns().span(), plan.target_columns().span());
    if (workspace.graph.vertices.array_size() != plan.tuple_size())
        throw std::invalid_argument("Distance: workspace vertex byte width does not match the plan.");
    detail::prepare_output(out, plan.output_columns().span(), { sources.get_storage_address(), edges.get_storage_address(), targets.get_storage_address() });

    auto& graph = workspace.graph;
    graph.clear();
    workspace.targets.clear();
    if (sources.empty() || targets.empty())
        return;
    const auto tuple_size = plan.tuple_size();
    graph.outgoing.reserve(edges.size());
    for (size_t i = 0; i < edges.size(); ++i)
    {
        const auto edge = edges.row(i);
        const auto from = graph.insert_vertex(edge.first(tuple_size));
        const auto to = graph.insert_vertex(edge.subspan(tuple_size));
        graph.outgoing.insert(from, to);
    }
    for (size_t i = 0; i < sources.size(); ++i)
        graph.insert_vertex(sources.row(i));
    workspace.targets.reserve(targets.size());
    for (size_t i = 0; i < targets.size(); ++i)
        workspace.targets.push_back(graph.insert_vertex(targets.row(i)));

    for (size_t i = 0; i < sources.size(); ++i)
    {
        const auto from = *graph.vertices.find(sources.row(i));
        detail::breadth_first_search(graph, from, workspace.distances, workspace.queue);
        for (const auto to : workspace.targets)
            if (workspace.distances[to] != detail::distance_infinity)
                out.insert(detail::make_distance_row(graph, from, to, workspace.distances[to], plan, workspace.row));
    }
}

template<ColumnTypes Values, RelationViewConcept<Values> S, RelationViewConcept<Values> E, RelationViewConcept<Values> T>
    requires ColumnValueFor<uint_t, Values>
Builder<Relation<Values>> distance(const S& sources, const E& edges, const T& targets, const DistancePlan<Values>& plan)
{
    Builder<Relation<Values>> out(plan.output_columns().span());
    DistanceWorkspace<Values> workspace(plan);
    distance(sources, edges, targets, plan, out, workspace);
    return out;
}

}  // namespace ygg::database

#endif
