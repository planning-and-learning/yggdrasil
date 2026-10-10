/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_INCREMENTAL_DETAILS_DISTANCE_REPAIR_HPP_
#define YGG_DATABASE_SEMANTICS_INCREMENTAL_DETAILS_DISTANCE_REPAIR_HPP_

#include "yggdrasil/containers/associative_containers.hpp"
#include "yggdrasil/containers/fibonacci_heap.hpp"
#include "yggdrasil/core/config.hpp"
#include "yggdrasil/core/counting_memory_resource.hpp"
#include "yggdrasil/semantics/equal_to.hpp"
#include "yggdrasil/semantics/hash.hpp"

#include <cassert>
#include <functional>
#include <limits>
#include <memory_resource>
#include <optional>
#include <utility>
#include <vector>

namespace ygg::database::incremental::detail
{
// Test targets enable these counters consistently across all translation units.
struct DistanceWork
{
    size_t processed_vertices = 0;
    size_t incoming_edges = 0;
    size_t outgoing_edges = 0;
    size_t heap_inserts = 0;
    size_t heap_updates = 0;
    size_t heap_erases = 0;
    size_t heap_pops = 0;
};
#ifdef YGG_DISTANCE_INSTRUMENTATION
inline thread_local DistanceWork distance_work;
#endif
inline void count_distance_work([[maybe_unused]] size_t DistanceWork::*member) noexcept
{
#ifdef YGG_DISTANCE_INSTRUMENTATION
    ++(distance_work.*member);
#endif
}

/// Shared across source trees: every heap is empty between completed repairs.
class DistanceRepairWorkspace
{
public:
    using Entry = std::pair<uint_t, uint_t>;  // Distance, then vertex (or predecessor in a local heap).
    using Heap = FibonacciHeap<Entry, std::greater<Entry>>;

private:
    // Resources outlive their heaps; the evaluator owns this workspace at a stable address.
    CountingMemoryResource m_memory;
    std::pmr::unsynchronized_pool_resource m_pool { &m_memory };
    Heap m_global { Heap::allocator_type { &m_pool } };
    std::vector<Heap> m_local;
    std::vector<std::optional<Heap::handle_type>> m_vertices;
    UnorderedMap<std::pair<uint_t, uint_t>, Heap::handle_type> m_edges;

    static void update(Heap& heap, Heap::handle_type handle, Entry entry)
    {
        if (*handle != entry)
        {
            heap.update(handle, entry);
            count_distance_work(&DistanceWork::heap_updates);
        }
    }

public:
    /// No vertex is scheduled and no edge candidate is pending: a repair is complete.
    bool empty() const noexcept { return m_global.empty() && m_edges.empty(); }
    /// Whether any vertex is scheduled for settling.
    bool scheduled() const noexcept { return !m_global.empty(); }
    /// Whether the vertex has no pending candidate distances.
    bool settled(uint_t vertex) const { return m_local.at(vertex).empty(); }

    void resize(size_t count)
    {
        assert(m_global.empty() && m_edges.empty());
        // Growing a vector may copy Boost heaps; all of them must be empty here.
        while (m_local.size() < count)
            m_local.emplace_back(Heap::allocator_type { &m_pool });
        m_vertices.resize(count);
    }

    void clear()
    {
        m_global.clear();
        for (auto& heap : m_local)
            heap.clear();
        for (auto& handle : m_vertices)
            handle.reset();
        m_edges.clear();
    }

    void set_edge(uint_t from, uint_t to, uint_t candidate, uint_t distance)
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

    size_t accept_improvement(uint_t vertex, uint_t distance)
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

    void schedule(uint_t vertex, uint_t distance, size_t supports)
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

    Entry pop()
    {
        const auto entry = m_global.top();
        m_global.pop();
        m_vertices[entry.second].reset();
        count_distance_work(&DistanceWork::heap_pops);
        return entry;
    }

    size_t memory_usage() const noexcept
    {
        return sizeof(*this) + m_memory.memory_usage() + m_local.capacity() * sizeof(Heap) + m_vertices.capacity() * sizeof(std::optional<Heap::handle_type>)
               + m_edges.capacity() * (sizeof(std::pair<const std::pair<uint_t, uint_t>, Heap::handle_type>) + sizeof(gtl::priv::ctrl_t));
    }
};
}  // namespace ygg::database::incremental::detail

#endif
