/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_DETAILS_DISTANCE_REPAIR_HPP_
#define YGG_DATABASE_DETAILS_DISTANCE_REPAIR_HPP_

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
struct DistanceRepairWorkspace
{
    using Entry = std::pair<uint_t, uint_t>;  // Distance, then vertex (or predecessor in a local heap).
    using Heap = FibonacciHeap<Entry, std::greater<Entry>>;

    // Resources outlive their heaps; the evaluator owns this workspace at a stable address.
    CountingMemoryResource memory;
    std::pmr::unsynchronized_pool_resource pool { &memory };
    Heap global { Heap::allocator_type { &pool } };
    std::vector<Heap> local;
    std::vector<std::optional<Heap::handle_type>> vertices;
    UnorderedMap<std::pair<uint_t, uint_t>, Heap::handle_type> edges;

    void resize(size_t count)
    {
        assert(global.empty() && edges.empty());
        // Growing a vector may copy Boost heaps; all of them must be empty here.
        while (local.size() < count)
            local.emplace_back(Heap::allocator_type { &pool });
        vertices.resize(count);
    }

    void clear()
    {
        global.clear();
        for (auto& heap : local)
            heap.clear();
        for (auto& handle : vertices)
            handle.reset();
        edges.clear();
    }

    static void update(Heap& heap, Heap::handle_type handle, Entry entry)
    {
        if (*handle != entry)
        {
            heap.update(handle, entry);
            count_distance_work(&DistanceWork::heap_updates);
        }
    }

    void set_edge(uint_t from, uint_t to, uint_t candidate, uint_t distance)
    {
        auto& heap = local[to];
        if (candidate >= distance && heap.empty())
            return;
        const auto edge = std::pair(from, to);
        const auto found = edges.find(edge);
        if (candidate < distance)
        {
            if (found == edges.end())
            {
                edges.emplace(edge, heap.push({ candidate, from }));
                count_distance_work(&DistanceWork::heap_inserts);
            }
            else
                update(heap, found->second, { candidate, from });
        }
        else if (found != edges.end())
        {
            heap.erase(found->second);
            edges.erase(found);
            count_distance_work(&DistanceWork::heap_erases);
        }
    }

    size_t accept_improvement(uint_t vertex, uint_t distance)
    {
        // Every support for the new minimum is already in this heap (Figure 4, lines 24–25).
        size_t supports = 0;
        auto& heap = local[vertex];
        for (const auto& [candidate, from] : heap)
        {
            supports += candidate == distance;
            edges.erase(std::pair(from, vertex));
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
        else if (!local[vertex].empty())
            key = local[vertex].top().first;
        auto& handle = vertices[vertex];
        if (key == infinity)
        {
            if (handle)
            {
                global.erase(*handle);
                handle.reset();
                count_distance_work(&DistanceWork::heap_erases);
            }
        }
        else if (handle)
            update(global, *handle, { key, vertex });
        else
        {
            handle = global.push({ key, vertex });
            count_distance_work(&DistanceWork::heap_inserts);
        }
    }

    Entry pop()
    {
        const auto entry = global.top();
        global.pop();
        vertices[entry.second].reset();
        count_distance_work(&DistanceWork::heap_pops);
        return entry;
    }

    size_t memory_usage() const noexcept
    {
        return sizeof(*this) + memory.memory_usage() + local.capacity() * sizeof(Heap) + vertices.capacity() * sizeof(std::optional<Heap::handle_type>)
               + edges.capacity() * (sizeof(std::pair<const std::pair<uint_t, uint_t>, Heap::handle_type>) + sizeof(gtl::priv::ctrl_t));
    }
};
}  // namespace ygg::database::incremental::detail

#endif
