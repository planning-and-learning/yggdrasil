/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef YGG_CONTAINERS_DETAIL_CONCURRENCY_HPP_
#define YGG_CONTAINERS_DETAIL_CONCURRENCY_HPP_

#include "yggdrasil/containers/detail/lazy_insert.hpp"
#include "yggdrasil/containers/detail/threading.hpp"

#include <cstddef>
#include <gtl/phmap.hpp>
#include <memory>
#include <mutex>
#include <oneapi/tbb/spin_rw_mutex.h>
#include <optional>
#include <shared_mutex>
#include <utility>

namespace ygg::detail
{

/**
 * Concurrent hash policy
 *
 * GTL's shard parameter is log2(submaps), so seven creates 128 independently
 * locked submaps. Seven-run repository benchmarks pinned to eight physical
 * cores selected this as the speed/memory knee: moving from six to seven shard
 * bits improved the important shared find/mixed workloads by up to 18%, while
 * eight bits doubled fixed container storage from roughly 16 KiB to 32 KiB
 * and regressed packed mixed throughput. With seven bits, SpinSharedMutex also
 * outperformed std::shared_mutex across every shared relation workload.
 *
 * spin_rw_mutex is a fast, unfair, writer-preferred spinning lock. It fits our
 * short critical sections and at most eight non-oversubscribed workers; changes
 * to either assumption require rerunning profiling/formalism/repository.cpp.
 * GTL treats unknown mutexes as exclusive-only, hence the reader-lock adapter
 * below. ThreadSafe=false still uses flat_hash_set without synchronization.
 * Indexed symbol insertion holds one hash shard while the storage append
 * transaction reserves the index, constructs the indexed value, and publishes
 * it. This keeps the embedded symbol index correct without a repository lock.
 * ponytail: Relation pool appends remain serialized to publish contiguous
 * stable indices; add range reservation only if profiling makes that lock
 * dominant.
 */
inline constexpr size_t kParallelHashShardBits = 7;

class SpinSharedMutex : public oneapi::tbb::spin_rw_mutex
{
};

}  // namespace ygg::detail

namespace gtl
{

template<>
class LockableImpl<ygg::detail::SpinSharedMutex> : public ygg::detail::SpinSharedMutex
{
public:
    using mutex_type = ygg::detail::SpinSharedMutex;
    using Base = LockableBaseImpl<mutex_type>;
    using SharedLock = std::shared_lock<mutex_type>;
    using UniqueLock = std::unique_lock<mutex_type>;
    using ReadWriteLock = typename Base::ReadWriteLock;
    using SharedLocks = typename Base::ReadLocks;
    using UniqueLocks = typename Base::WriteLocks;
};

}  // namespace gtl

namespace ygg::detail
{

template<typename T, typename Hash, typename EqualTo, bool ThreadSafe>
struct HashSet;

template<typename T, typename Hash, typename EqualTo>
struct HashSet<T, Hash, EqualTo, false>
{
    using type = gtl::flat_hash_set<T, Hash, EqualTo>;
};

template<typename T, typename Hash, typename EqualTo>
struct HashSet<T, Hash, EqualTo, true>
{
    using type = gtl::parallel_flat_hash_set<T, Hash, EqualTo, std::allocator<T>, kParallelHashShardBits, SpinSharedMutex>;
};

template<typename T, typename Hash, typename EqualTo, bool ThreadSafe>
using HashSetType = typename HashSet<T, Hash, EqualTo, ThreadSafe>::type;

template<bool ThreadSafe, typename Set, typename Key>
std::optional<typename Set::value_type> find_value_with_hash(const Set& set, const Key& key, size_t hash)
{
    if constexpr (ThreadSafe)
    {
        std::optional<typename Set::value_type> result;
        set.with_submap(set.subidx(hash),
                        [&](const auto& submap)
                        {
                            if (const auto it = submap.find(key, hash); it != submap.end())
                                result = *it;
                        });
        return result;
    }
    else
    {
        if (const auto it = set.find(key, hash); it != set.end())
            return *it;
        return std::nullopt;
    }
}

/**
 * Performs a prehashed lookup without locking a concurrent hash shard.
 *
 * All prior publication must happen-before the call. The set must remain alive
 * and must not be mutated, cleared, rehashed, moved, or destroyed for the
 * whole call. Concurrent read-only calls are allowed. Violating these
 * preconditions is a data race and undefined behavior.
 *
 * GTL has no prehashed overload of if_contains_unsafe(), so the dependency's
 * explicitly unsafe submap access is centralized here.
 */
template<bool ThreadSafe, typename Set, typename Key>
std::optional<typename Set::value_type> find_value_unsafe_with_hash(const Set& set, const Key& key, size_t hash)
{
    if constexpr (ThreadSafe)
    {
        const auto& submap = set.get_inner(set.subidx(hash)).set_;
        if (const auto it = submap.find(key, hash); it != submap.end())
            return *it;
        return std::nullopt;
    }
    else
    {
        if (const auto it = set.find(key, hash); it != set.end())
            return *it;
        return std::nullopt;
    }
}

template<typename Set, typename Key, typename Factory>
std::pair<typename Set::value_type, bool> parallel_lazy_insert_with_hash(Set& set, const Key& key, size_t hash, Factory&& factory)
{
    auto result = std::pair<typename Set::value_type, bool> {};
    set.with_submap_m(set.subidx(hash),
                      [&](auto& submap)
                      {
                          const auto [it, inserted] = lazy_insert_with_hash(submap, key, hash, std::forward<Factory>(factory));
                          // Iterators are not safe after releasing the submap lock.
                          result = { *it, inserted };
                      });
    return result;
}

template<bool ThreadSafe, typename Set, typename Key, typename Factory>
std::pair<typename Set::value_type, bool> complete_miss_value_with_hash(Set& set, const Key& key, size_t hash, Factory&& factory)
{
    if constexpr (ThreadSafe)
        return parallel_lazy_insert_with_hash(set, key, hash, std::forward<Factory>(factory));
    else
        return lazy_insert_value_with_hash(set, key, hash, std::forward<Factory>(factory));
}

template<bool ThreadSafe, typename Set, typename Key, typename Factory>
std::pair<typename Set::value_type, bool> find_or_lazy_insert_value_with_hash(Set& set, const Key& key, size_t hash, Factory&& factory)
{
    if constexpr (ThreadSafe)
    {
        if (const auto existing = find_value_with_hash<true>(set, key, hash))
            return { *existing, false };
        return parallel_lazy_insert_with_hash(set, key, hash, std::forward<Factory>(factory));
    }
    else
    {
        return lazy_insert_value_with_hash(set, key, hash, std::forward<Factory>(factory));
    }
}

template<typename Set>
size_t hash_set_memory_usage(const Set& set) noexcept
{
    // The parallel set's fixed shard objects are part of the container itself,
    // while this reports retained dynamic payload and control storage.
    return set.capacity() * (sizeof(typename Set::value_type) + sizeof(gtl::priv::ctrl_t));
}

}  // namespace ygg::detail

#endif
