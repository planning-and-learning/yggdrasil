/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_DISTANCE_HPP_
#define YGG_DATABASE_SEMANTICS_DISTANCE_HPP_

#include "yggdrasil/containers/raw_array_set.hpp"
#include "yggdrasil/database/semantics/distance_plan.hpp"
#include "yggdrasil/database/semantics/operations.hpp"

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace ygg::database
{

namespace detail
{
/// Append-only tuple IDs remain valid until clear(); adjacency stores IDs only.
struct DistanceGraph
{
    static constexpr size_t first_segment_bytes = 64;
    RawArraySet<std::byte, first_segment_bytes> vertices;
    UnorderedMultiMap<uint_t, uint_t> outgoing;

    explicit DistanceGraph(size_t tuple_bytes);
    uint_t insert_vertex(std::span<const std::byte> tuple);
    /// Inserts both endpoint tuples of an edge row; returns their IDs.
    std::pair<uint_t, uint_t> insert_endpoints(std::span<const std::byte> edge);
    bool contains_edge(uint_t from, uint_t to) const;
    void clear();
    size_t memory_usage() const noexcept;
};
}  // namespace detail

/// Reusable full-evaluation storage for one vertex byte width. Each call replaces
/// its graph; sequential calls retain capacity. Do not share across concurrent calls.
class DistanceWorkspace
{
    detail::DistanceGraph m_graph;
    std::vector<uint_t> m_distances;
    std::vector<uint_t> m_queue;
    std::vector<uint_t> m_targets;
    std::vector<std::byte> m_row;

public:
    explicit DistanceWorkspace(const DistancePlan& plan);

    /// Whether the workspace serves the plan's vertex byte width.
    bool matches(const DistancePlan& plan) const noexcept;
    /// Appends the distances of the inputs, which match the plan, to out.
    template<RelationViewConcept S, RelationViewConcept E, RelationViewConcept T>
    void append_distances(const S& sources, const E& edges, const T& targets, const DistancePlan& plan, Builder<Relation>& out);
    size_t memory_usage() const noexcept;
};

/// Exact directed unit-edge distances for every reachable source/target pair.
/// Includes zero-length paths; unreachable pairs are absent. Input and output
/// schemas and aliases are checked before clearing output. Evaluation failures
/// may leave partial output. Inputs must not borrow the workspace's buffers.
template<RelationViewConcept S, RelationViewConcept E, RelationViewConcept T>
void distance(const S& sources,
              const E& edges,
              const T& targets,
              const DistancePlan& plan,
              Builder<Relation>& out,
              DistanceWorkspace& workspace);

template<RelationViewConcept S, RelationViewConcept E, RelationViewConcept T>
Builder<Relation> distance(const S& sources, const E& edges, const T& targets, const DistancePlan& plan);

/// The smallest distance between any source and target, from distance() into result.
/// uint_t's maximum if no target is reachable.
template<RelationViewConcept S, RelationViewConcept E, RelationViewConcept T>
uint_t min_distance(const S& sources,
                    const E& edges,
                    const T& targets,
                    const DistancePlan& plan,
                    Builder<Relation>& result,
                    DistanceWorkspace& workspace);

}  // namespace ygg::database

#include "yggdrasil/database/semantics/details/distance.hpp"

#endif
