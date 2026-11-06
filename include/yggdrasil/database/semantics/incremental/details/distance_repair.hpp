/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_INCREMENTAL_DETAILS_DISTANCE_REPAIR_HPP_
#define YGG_DATABASE_SEMANTICS_INCREMENTAL_DETAILS_DISTANCE_REPAIR_HPP_

#include "yggdrasil/core/config.hpp"

#include <urd/elements/equal_to.hpp>
#include <urd/elements/hash.hpp>
#include <urd/memory.hpp>
#include <urd/repr/maps/std_map.hpp>
#include <urd/repr/maps/unordered_flat_map.hpp>
#include <urd/repr/queues/addressable_heap.hpp>
#include <urd/repr/values/unordered_flat_set.hpp>
#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>

#include <cassert>
#include <limits>
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
inline DistanceWork& distance_work() noexcept
{
    thread_local DistanceWork work;
    return work;
}
#endif
void count_distance_work([[maybe_unused]] size_t DistanceWork::* member) noexcept;

/// Shared across source trees: every heap is empty between completed repairs.
class DistanceRepairWorkspace
{
    using Entry = std::pair<uint_t, uint_t>;  // Distance, then vertex (or predecessor in a local heap).
    // The least entry leaves first; push gives a handle that update and erase take.
    using Heap = urd::Store<urd::queue<Entry>, urd::by_priority, urd::handles>;

    Heap m_global;
    urd::Store<urd::sequence<Heap>> m_local;
    urd::Store<urd::sequence<std::optional<Heap::handle_type>>> m_vertices;
    urd::Store<urd::map<std::pair<uint_t, uint_t>, Heap::handle_type>> m_edges;

    static void update(Heap& heap, Heap::handle_type handle, Entry entry)
    {
        if (heap[handle] != entry)
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
        if (m_local.size() < count)
            m_local.resize(count);
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

    void set_edge(uint_t from, uint_t to, uint_t candidate, uint_t distance);

    size_t accept_improvement(uint_t vertex, uint_t distance);

    void schedule(uint_t vertex, uint_t distance, size_t supports);

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
        auto heaps = urd::memory_usage(m_global);
        for (const auto& heap : m_local)
            heaps += urd::memory_usage(heap);
        return sizeof(*this) + heaps + m_local.capacity() * sizeof(Heap) + m_vertices.capacity() * sizeof(std::optional<Heap::handle_type>)
               + urd::memory_usage(m_edges);
    }
};
}  // namespace ygg::database::incremental::detail

#endif
