/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_DISTANCE_HPP_
#define YGG_DATABASE_DISTANCE_HPP_

#include "yggdrasil/containers/raw_array_set.hpp"
#include "yggdrasil/database/operations.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace ygg::database
{

/// Sources and targets contain k-field vertices; each edge contains two such
/// vertices. Positional field types must agree, independently of column labels.
/// Output columns are the edge columns followed by a fresh uint_t distance column.
template<ColumnTypes Values = DefaultColumnTypes>
class DistancePlan
{
    Builder<Columns<Values>> m_sources;
    Builder<Columns<Values>> m_edges;
    Builder<Columns<Values>> m_targets;
    Builder<Columns<Values>> m_output;

public:
    DistancePlan() = default;
    DistancePlan(std::span<const ColumnLayout> sources,
                 std::span<const ColumnLayout> edges,
                 std::span<const ColumnLayout> targets,
                 Index<Column> distance_column);

    auto source_columns() const& noexcept { return make_view(m_sources, *this); }
    auto source_columns() const&& = delete;
    auto edge_columns() const& noexcept { return make_view(m_edges, *this); }
    auto edge_columns() const&& = delete;
    auto target_columns() const& noexcept { return make_view(m_targets, *this); }
    auto target_columns() const&& = delete;
    auto output_columns() const& noexcept { return make_view(m_output, *this); }
    auto output_columns() const&& = delete;
    size_t tuple_size() const noexcept { return m_sources.row_size(); }
    size_t arity() const noexcept { return m_sources.size(); }
    auto cista_members() noexcept { return std::tie(m_sources, m_edges, m_targets, m_output); }
    auto cista_members() const noexcept { return std::tie(m_sources, m_edges, m_targets, m_output); }
};

namespace detail
{
/// Append-only tuple IDs remain valid until clear(); adjacency stores IDs only.
struct DistanceGraph
{
    RawArraySet<std::byte, 64> vertices;
    UnorderedMultiMap<uint_t, uint_t> outgoing;

    explicit DistanceGraph(size_t tuple_bytes);
    uint_t insert_vertex(std::span<const std::byte> tuple);
    bool contains_edge(uint_t from, uint_t to) const;
    void clear();
    size_t memory_usage() const noexcept;
};
}  // namespace detail

/// Reusable full-evaluation storage for one vertex byte width. Each call replaces
/// its graph; sequential calls retain capacity. Do not share across concurrent calls.
template<ColumnTypes Values = DefaultColumnTypes>
    requires ColumnValueFor<uint_t, Values>
struct DistanceWorkspace
{
    detail::DistanceGraph graph;
    std::vector<uint_t> distances;
    std::vector<uint_t> queue;
    std::vector<uint_t> targets;
    std::vector<std::byte> row;

    explicit DistanceWorkspace(const DistancePlan<Values>& plan);
    size_t memory_usage() const noexcept;
};

/// Exact directed unit-edge distances for every reachable source/target pair.
/// Includes zero-length paths; unreachable pairs are absent. Input and output
/// schemas and aliases are checked before clearing output. Evaluation failures
/// may leave partial output. Inputs must not borrow the workspace's buffers.
template<ColumnTypes Values, RelationViewConcept<Values> S, RelationViewConcept<Values> E, RelationViewConcept<Values> T>
    requires ColumnValueFor<uint_t, Values>
void distance(const S& sources,
              const E& edges,
              const T& targets,
              const DistancePlan<Values>& plan,
              Builder<Relation<Values>>& out,
              DistanceWorkspace<Values>& workspace);

template<ColumnTypes Values, RelationViewConcept<Values> S, RelationViewConcept<Values> E, RelationViewConcept<Values> T>
    requires ColumnValueFor<uint_t, Values>
Builder<Relation<Values>> distance(const S& sources, const E& edges, const T& targets, const DistancePlan<Values>& plan);

}  // namespace ygg::database

#include "yggdrasil/database/details/distance.hpp"

#endif
