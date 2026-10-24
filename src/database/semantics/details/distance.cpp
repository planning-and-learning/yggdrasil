/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/details/distance.hpp"

namespace ygg::database
{
namespace detail
{
uint_t DistanceGraph::insert_vertex(std::span<const std::byte> tuple)
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
}
}

namespace ygg::database
{
namespace detail
{
std::pair<uint_t, uint_t> DistanceGraph::insert_endpoints(std::span<const std::byte> edge)
{
    const auto from = insert_vertex(edge.first(vertices.array_size()));
    return { from, insert_vertex(edge.subspan(vertices.array_size())) };
}
}
}

namespace ygg::database
{
namespace detail
{
bool DistanceGraph::contains_edge(uint_t from, uint_t to) const
{
    for (const auto target : outgoing.values(from))
        if (target == to)
            return true;
    return false;
}
}
}

namespace ygg::database
{
namespace detail
{
void DistanceGraph::clear()
{
    outgoing.clear();
    vertices.clear();
}
}
}

namespace ygg::database
{
namespace detail
{
void breadth_first_search(const DistanceGraph& graph, uint_t root, std::vector<uint_t>& distances, std::vector<uint_t>& queue)
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
}
}

namespace ygg::database
{
namespace detail
{
Row make_distance_row(const DistanceGraph& graph, uint_t from, uint_t to, uint_t length, const DistancePlan& plan, std::vector<std::byte>& row)
{
    assert(length != distance_infinity);
    row.resize(plan.output_columns().row_size());
    const auto tuple_size = plan.tuple_size();
    std::ranges::copy(graph.vertices[from], row.begin());
    std::ranges::copy(graph.vertices[to], row.begin() + tuple_size);
    ByteCodec<uint_t>::encode(length, std::span(row).subspan(2 * tuple_size));
    return Row(row, plan.output_columns().span());
}
}
}

namespace ygg::database
{
DistancePlan::DistancePlan(std::span<const ColumnLayout> sources,
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
    m_output.template push_back<uint_t>(distance_column);
}
}

namespace ygg::database
{
DistanceWorkspace::DistanceWorkspace(const DistancePlan& plan) : m_graph(plan.tuple_size()) {}
}

namespace ygg::database
{
bool DistanceWorkspace::matches(const DistancePlan& plan) const noexcept { return m_graph.vertices.array_size() == plan.tuple_size(); }
}

namespace ygg::database
{
size_t DistanceWorkspace::memory_usage() const noexcept
{ return m_graph.memory_usage() + (m_distances.capacity() + m_queue.capacity() + m_targets.capacity()) * sizeof(uint_t) + m_row.capacity(); }
}
