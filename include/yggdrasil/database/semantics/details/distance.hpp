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

inline size_t DistanceGraph::memory_usage() const noexcept { return vertices.memory_usage() + outgoing.memory_usage(); }

void breadth_first_search(const DistanceGraph& graph, uint_t root, std::vector<uint_t>& distances, std::vector<uint_t>& queue);

Row make_distance_row(const DistanceGraph& graph, uint_t from, uint_t to, uint_t length, const DistancePlan& plan, std::vector<std::byte>& row);
}  // namespace detail

template<RelationViewConcept S, RelationViewConcept E, RelationViewConcept T>
void DistanceWorkspace::append_distances(const S& sources, const E& edges, const T& targets, const DistancePlan& plan, Builder<Relation>& out)
{
    m_graph.clear();
    m_targets.clear();
    if (sources.empty() || targets.empty())
        return;
    m_graph.outgoing.reserve(edges.size());
    for (size_t i = 0; i < edges.size(); ++i)
    {
        const auto [from, to] = m_graph.insert_endpoints(edges.row(i));
        m_graph.outgoing.insert(from, to);
    }
    for (size_t i = 0; i < sources.size(); ++i)
        m_graph.insert_vertex(sources.row(i));
    m_targets.reserve(targets.size());
    for (size_t i = 0; i < targets.size(); ++i)
        m_targets.push_back(m_graph.insert_vertex(targets.row(i)));

    for (size_t i = 0; i < sources.size(); ++i)
    {
        const auto from = *m_graph.vertices.find(sources.row(i));
        detail::breadth_first_search(m_graph, from, m_distances, m_queue);
        for (const auto to : m_targets)
            if (m_distances[to] != detail::distance_infinity)
                out.insert(detail::make_distance_row(m_graph, from, to, m_distances[to], plan, m_row));
    }
}

template<RelationViewConcept S, RelationViewConcept E, RelationViewConcept T>
void distance(const S& sources, const E& edges, const T& targets, const DistancePlan& plan, Builder<Relation>& out, DistanceWorkspace& workspace)
{
    detail::require_plan_columns(sources.columns().span(), plan.source_columns().span());
    detail::require_plan_columns(edges.columns().span(), plan.edge_columns().span());
    detail::require_plan_columns(targets.columns().span(), plan.target_columns().span());
    if (!workspace.matches(plan))
        throw std::invalid_argument("Distance: workspace vertex byte width does not match the plan.");
    detail::prepare_output(out, plan.output_columns().span(), { sources.get_storage_address(), edges.get_storage_address(), targets.get_storage_address() });
    workspace.append_distances(sources, edges, targets, plan, out);
}

template<RelationViewConcept S, RelationViewConcept E, RelationViewConcept T>
Builder<Relation> distance(const S& sources, const E& edges, const T& targets, const DistancePlan& plan)
{
    Builder<Relation> out(plan.output_columns().span());
    DistanceWorkspace workspace(plan);
    distance(sources, edges, targets, plan, out, workspace);
    return out;
}

template<RelationViewConcept S, RelationViewConcept E, RelationViewConcept T>
uint_t min_distance(const S& sources, const E& edges, const T& targets, const DistancePlan& plan, Builder<Relation>& result, DistanceWorkspace& workspace)
{
    distance(sources, edges, targets, plan, result, workspace);
    const auto columns = result.columns().span();
    const auto position = columns.size() - 1;  // The distance column follows the edge columns.
    auto best = std::numeric_limits<uint_t>::max();
    for (size_t i = 0; i < result.size(); ++i)
        best = std::min(best, Row(result.row(i), columns).template get<uint_t>(position));
    return best;
}

}  // namespace ygg::database

#endif
