/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/distance.hpp"
#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>

#include <span>

namespace ygg::database::detail
{
VertexSet::handle_type DistanceGraph::insert_vertex(std::span<const Value> tuple)
{
    // Reserve the maximum distance as infinity; a simple path has fewer edges than vertices.
    if (vertices.size() >= distance_infinity)
    {
        if (const auto existing = vertices.find(tuple); !existing.is_none())
            return existing;
        throw std::length_error("Distance: vertex count exceeds the finite distance range.");
    }
    return vertices.insert(tuple).first;
}
}

namespace ygg::database::detail
{
std::pair<VertexSet::handle_type, VertexSet::handle_type> DistanceGraph::insert_endpoints(const RowPool::row_type& edge)
{
    const auto values = vertex_values(edge, scratch);
    const auto from = insert_vertex(values.first(tuple_size));
    return { from, insert_vertex(values.subspan(tuple_size, tuple_size)) };
}
}

namespace ygg::database::detail
{
bool DistanceGraph::contains_edge(VertexSet::handle_type from, VertexSet::handle_type to) const
{
    for (const auto target : outgoing.values(from))
        if (target == to)
            return true;
    return false;
}
}

namespace ygg::database::detail
{
void DistanceGraph::clear()
{
    outgoing.clear();
    vertices.clear();
}
}

namespace ygg::database::detail
{
void breadth_first_search(const DistanceGraph& graph,
                          VertexSet::handle_type root,
                          urd::Store<urd::sequence<uint_t, VertexSet::handle_type>>& distances,
                          urd::Store<urd::sequence<VertexSet::handle_type>>& queue)
{
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
}

namespace ygg::database::detail
{
std::span<const Value> make_distance_row(const DistanceGraph& graph,
                                         VertexSet::handle_type from,
                                         VertexSet::handle_type to,
                                         uint_t length,
                                         urd::Store<urd::sequence<Value>>& row)
{
    assert(length != distance_infinity);
    row.clear();
    const auto source = graph.vertices.view(from);
    const auto target = graph.vertices.view(to);
    row.insert(row.end(), source.begin(), source.end());
    row.insert(row.end(), target.begin(), target.end());
    row.push_back(encode_value(length));
    return row;
}
}

namespace ygg::database
{
DistancePlan::DistancePlan(std::span<const ColumnLayout> sources,
                           std::span<const ColumnLayout> edges,
                           std::span<const ColumnLayout> targets,
                           urd::index<Column> distance_column)
{
    for (const auto columns : { sources, edges, targets })
        validate_columns(columns);
    m_sources.set(sources.begin(), sources.end());
    m_edges.set(edges.begin(), edges.end());
    m_targets.set(targets.begin(), targets.end());
    m_output.set(edges.begin(), edges.end());
    const auto k = arity();
    if (m_targets.size() != k || m_edges.size() % 2 != 0 || m_edges.size() / 2 != k)
        throw std::invalid_argument("Distance: input arities must be k, 2k, k.");
    for (size_t i = 0; i < k; ++i)
        if (m_sources[i].type != m_targets[i].type || m_sources[i].type != m_edges[i].type || m_sources[i].type != m_edges[k + i].type)
            throw std::invalid_argument("Distance: vertex field types must agree by position.");
    m_output.push_back(ColumnLayout { distance_column, type_id<uint_t> });
    validate_columns(output_columns());
}
}

namespace ygg::database
{
DistanceWorkspace::DistanceWorkspace(const DistancePlan& plan) : m_graph(plan.tuple_size()) {}
}

namespace ygg::database
{
bool DistanceWorkspace::matches(const DistancePlan& plan) const noexcept { return m_graph.tuple_size == plan.tuple_size(); }
}

namespace ygg::database
{
size_t DistanceWorkspace::memory_usage() const noexcept
{
    return m_graph.memory_usage() + m_distances.capacity() * sizeof(uint_t) + (m_queue.capacity() + m_targets.capacity()) * sizeof(VertexSet::handle_type)
           + m_row.capacity() * sizeof(Value);
}
}
