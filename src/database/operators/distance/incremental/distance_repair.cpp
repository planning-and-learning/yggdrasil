/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/operators/distance/incremental/detail/distance_repair.hpp"

namespace ygg::database::incremental::detail
{
void count_distance_work([[maybe_unused]] size_t DistanceWork::* member) noexcept
{
#ifdef YGG_DISTANCE_INSTRUMENTATION
    ++(distance_work().*member);
#endif
}
}

namespace ygg::database::incremental::detail
{
void DistanceRepairWorkspace::set_edge(uint_t from, uint_t to, uint_t candidate, uint_t distance)
{
    auto& heap = m_local[to];
    if (candidate >= distance && heap.empty())
        return;
    const auto edge = std::pair(from, to);
    const auto found = m_edges.find(edge);
    if (candidate < distance)
    {
        if (found == m_edges.end())
        {
            m_edges.emplace(edge, heap.push({ candidate, from }));
            count_distance_work(&DistanceWork::heap_inserts);
        }
        else
            update(heap, found->second, { candidate, from });
    }
    else if (found != m_edges.end())
    {
        heap.erase(found->second);
        m_edges.erase(found);
        count_distance_work(&DistanceWork::heap_erases);
    }
}
}

namespace ygg::database::incremental::detail
{
size_t DistanceRepairWorkspace::accept_improvement(uint_t vertex, uint_t distance)
{
    // Every support for the new minimum is already in this heap (Figure 4, lines 24–25).
    size_t supports = 0;
    auto& heap = m_local[vertex];
    for (const auto& [candidate, from] : heap)
    {
        supports += candidate == distance;
        m_edges.erase(std::pair(from, vertex));
        count_distance_work(&DistanceWork::heap_erases);
    }
    heap.clear();
    return supports;
}
}

namespace ygg::database::incremental::detail
{
void DistanceRepairWorkspace::schedule(uint_t vertex, uint_t distance, size_t supports)
{
    constexpr auto infinity = std::numeric_limits<uint_t>::max();
    auto key = infinity;
    if (distance != infinity && supports == 0)
        key = distance;
    else if (!m_local[vertex].empty())
        key = m_local[vertex].top().first;
    auto& handle = m_vertices[vertex];
    if (key == infinity)
    {
        if (handle)
        {
            m_global.erase(*handle);
            handle.reset();
            count_distance_work(&DistanceWork::heap_erases);
        }
    }
    else if (handle)
        update(m_global, *handle, { key, vertex });
    else
    {
        handle = m_global.push({ key, vertex });
        count_distance_work(&DistanceWork::heap_inserts);
    }
}
}
