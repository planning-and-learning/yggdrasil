/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_DISTANCE_HPP_
#define YGG_DATABASE_SEMANTICS_DISTANCE_HPP_

#include <urd/repr/sequences/sequence.hpp>
#include <urd/repr/pools/pools.hpp>
#include <urd/store.hpp>
#include "yggdrasil/core/config.hpp"
#include <urd/config.hpp>
#include "yggdrasil/database/semantics/distance_plan.hpp"
#include "yggdrasil/database/semantics/operations.hpp"
#include "yggdrasil/database/semantics/search/concepts.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ygg::database
{

namespace detail
{
/// Append-only vertex handles remain valid until clear(); adjacency stores handles only.
struct DistanceGraph
{
    size_t tuple_bytes;
    /// The vertices' tuples, interned in a urd pool.
    VertexSet vertices;
    urd::Store<urd::map<VertexSet::handle_type, VertexSet::handle_type>, urd::multi, urd::retains> outgoing;

    explicit DistanceGraph(size_t tuple_bytes);
    VertexSet::handle_type insert_vertex(std::span<const std::byte> tuple);
    /// Inserts both endpoint tuples of an edge row; returns their handles.
    std::pair<VertexSet::handle_type, VertexSet::handle_type> insert_endpoints(std::span<const std::byte> edge);
    [[nodiscard]] bool contains_edge(VertexSet::handle_type from, VertexSet::handle_type to) const;
    void clear();
    [[nodiscard]] size_t memory_usage() const noexcept;
};
}  // namespace detail

/// Reusable full-evaluation storage for one vertex byte width. Each call replaces
/// its graph; sequential calls retain capacity. Do not share across concurrent calls.
class DistanceWorkspace
{
    detail::DistanceGraph m_graph;
    urd::Store<urd::sequence<uint_t, VertexSet::handle_type>> m_distances;
    urd::Store<urd::sequence<VertexSet::handle_type>> m_queue;
    urd::Store<urd::sequence<VertexSet::handle_type>> m_targets;
    urd::Store<urd::sequence<std::byte>> m_row;

public:
    explicit DistanceWorkspace(const DistancePlan& plan);

    /// Whether the workspace serves the plan's vertex byte width.
    [[nodiscard]] bool matches(const DistancePlan& plan) const noexcept;
    /// Appends the distances of the inputs, which match the plan, to out.
    template<RelationViewConcept S, RelationViewConcept E, RelationViewConcept T>
    void append_distances(const S& sources, const E& edges, const T& targets, const DistancePlan& plan, urd::builder<Relation>& out);
    [[nodiscard]] size_t memory_usage() const noexcept;
};

/// Exact directed unit-edge distances for every reachable source/target pair.
/// Includes zero-length paths; unreachable pairs are absent. Input and output
/// schemas and aliases are checked before clearing output. Evaluation failures
/// may leave partial output. Inputs must not borrow the workspace's buffers.
template<RelationViewConcept S, RelationViewConcept E, RelationViewConcept T>
void distance(const S& sources, const E& edges, const T& targets, const DistancePlan& plan, urd::builder<Relation>& out, DistanceWorkspace& workspace);

template<RelationViewConcept S, RelationViewConcept E, RelationViewConcept T>
urd::builder<Relation> distance(const S& sources, const E& edges, const T& targets, const DistancePlan& plan);

/// The smallest distance between any source and target, from distance() into result.
/// uint_t's maximum if no target is reachable.
template<RelationViewConcept S, RelationViewConcept E, RelationViewConcept T>
uint_t min_distance(const S& sources, const E& edges, const T& targets, const DistancePlan& plan, urd::builder<Relation>& result, DistanceWorkspace& workspace);

}  // namespace ygg::database

namespace ygg::database
{

namespace detail
{
inline constexpr uint_t distance_infinity = std::numeric_limits<uint_t>::max();

inline DistanceGraph::DistanceGraph(size_t tuple_bytes_) : tuple_bytes(tuple_bytes_) {}

inline size_t DistanceGraph::memory_usage() const noexcept { return urd::memory_usage(vertices) + urd::memory_usage(outgoing); }

void breadth_first_search(const DistanceGraph& graph,
                          VertexSet::handle_type root,
                          urd::Store<urd::sequence<uint_t, VertexSet::handle_type>>& distances,
                          urd::Store<urd::sequence<VertexSet::handle_type>>& queue);

Row make_distance_row(const DistanceGraph& graph,
                      VertexSet::handle_type from,
                      VertexSet::handle_type to,
                      uint_t length,
                      const DistancePlan& plan,
                      urd::Store<urd::sequence<std::byte>>& row);
}  // namespace detail

template<RelationViewConcept S, RelationViewConcept E, RelationViewConcept T>
void DistanceWorkspace::append_distances(const S& sources, const E& edges, const T& targets, const DistancePlan& plan, urd::builder<Relation>& out)
{
    m_graph.clear();
    m_targets.clear();
    if (sources.empty() || targets.empty())
        return;
    m_graph.outgoing.reserve(edges.size());
    for (size_t i = 0; i < edges.size(); ++i)
    {
        const auto [from, to] = m_graph.insert_endpoints(edges.row(i));
        m_graph.outgoing.insert({ from, to });
    }
    for (size_t i = 0; i < sources.size(); ++i)
        m_graph.insert_vertex(sources.row(i));
    m_targets.reserve(targets.size());
    for (size_t i = 0; i < targets.size(); ++i)
        m_targets.push_back(m_graph.insert_vertex(targets.row(i)));

    for (size_t i = 0; i < sources.size(); ++i)
    {
        const auto from = m_graph.vertices.find(sources.row(i));
        URD_ASSUME(!from.is_none());  // every source was inserted above
        detail::breadth_first_search(m_graph, from, m_distances, m_queue);
        for (const auto to : m_targets)
            if (m_distances[to] != detail::distance_infinity)
                out.insert(detail::make_distance_row(m_graph, from, to, m_distances[to], plan, m_row));
    }
}

template<RelationViewConcept S, RelationViewConcept E, RelationViewConcept T>
void distance(const S& sources, const E& edges, const T& targets, const DistancePlan& plan, urd::builder<Relation>& out, DistanceWorkspace& workspace)
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
urd::builder<Relation> distance(const S& sources, const E& edges, const T& targets, const DistancePlan& plan)
{
    urd::builder<Relation> out(plan.output_columns().span());
    DistanceWorkspace workspace(plan);
    distance(sources, edges, targets, plan, out, workspace);
    return out;
}

template<RelationViewConcept S, RelationViewConcept E, RelationViewConcept T>
uint_t min_distance(const S& sources, const E& edges, const T& targets, const DistancePlan& plan, urd::builder<Relation>& result, DistanceWorkspace& workspace)
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
