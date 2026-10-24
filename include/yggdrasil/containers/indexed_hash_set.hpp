/*
 * Copyright (C) 2025-2026 Dominik Drexler
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

#ifndef YGG_CONTAINERS_INDEXED_HASH_SET_HPP_
#define YGG_CONTAINERS_INDEXED_HASH_SET_HPP_

#include "yggdrasil/containers/detail/segmented_rows.hpp"
#include "yggdrasil/containers/segment_sizes.hpp"
#include "yggdrasil/core/config.hpp"
#include "yggdrasil/core/types.hpp"
#include "yggdrasil/semantics/equal_to.hpp"
#include "yggdrasil/semantics/hash.hpp"

#include <cstddef>
#include <gtl/phmap_utils.hpp>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace ygg
{

/// ThreadSafe permits concurrent normal lookup, insertion, size queries, and reads of
/// published indices. Hash-table locking is limited to the target shard and
/// the storage append lock covers index allocation, construction, and
/// publication. Clear, memory inspection, move, and destruction require
/// quiescence. find_unsafe_with_hash additionally requires the documented
/// caller-enforced quiescence.
///
/// Reads by index are defined here. The elements, the value-to-index lookup, construction, and the write API live behind
/// one pointer and are defined in detail/indexed_hash_set_impl.hpp; sources that instantiate the set include it.
template<typename Tag,
         HashFor<Data<Tag>> H = Hash<Data<Tag>>,
         EqualToFor<Data<Tag>> E = EqualTo<Data<Tag>>,
         size_t FirstSegmentSize = default_first_segment_size,
         bool ThreadSafe = false>
class IndexedHashSet
{
private:
    class Store;

    using Rows = detail::SegmentedRows<Data<Tag>, FirstSegmentSize, ThreadSafe, uint_t>;

    template<typename Factory>
    Index<Tag> append_new_with_index(Factory&& factory);

    Index<Tag> append_new(const Data<Tag>& element);

public:
    static constexpr bool thread_safe = ThreadSafe;

    IndexedHashSet();
    IndexedHashSet(const IndexedHashSet& other) = delete;
    IndexedHashSet& operator=(const IndexedHashSet& other) = delete;
    IndexedHashSet(IndexedHashSet&& other) noexcept;
    IndexedHashSet& operator=(IndexedHashSet&& other) noexcept;
    ~IndexedHashSet();

    void clear() noexcept;

    static size_t hash(const Data<Tag>& element) noexcept(std::is_nothrow_default_constructible_v<H> && std::is_nothrow_invocable_v<const H&, const Data<Tag>&>)
    {
        return gtl::phmap_mix<sizeof(size_t)>()(H {}(element));
    }

    std::optional<Index<Tag>> find_with_hash(const Data<Tag>& element, size_t h) const;

    /// Performs a prehashed lookup without a shard lock. Prior publication must
    /// happen-before the call, and the set must remain alive and unmodified for
    /// the whole call. Concurrent read-only calls are allowed. No runtime check
    /// enforces these preconditions; violating them is undefined behavior.
    std::optional<Index<Tag>> find_unsafe_with_hash(const Data<Tag>& element, size_t h) const;

    std::optional<Index<Tag>> find(const Data<Tag>& element) const { return find_with_hash(element, IndexedHashSet::hash(element)); }

    bool contains_with_hash(const Data<Tag>& element, size_t h) const { return find_with_hash(element, h).has_value(); }

    bool contains(const Data<Tag>& element) const { return find(element).has_value(); }

    std::pair<Index<Tag>, bool> insert_with_hash(size_t h, const Data<Tag>& element);

    /// Rechecks a caller-observed miss and returns the canonical stored index.
    std::pair<Index<Tag>, bool> complete_miss_with_hash(size_t h, const Data<Tag>& element);

    /// Runs the factory inside the insertion transaction with the reserved local index.
    /// The result must preserve the key's identity, and the factory must not access this set.
    template<typename Factory>
    std::pair<Index<Tag>, bool> complete_miss_with_hash(size_t h, const Data<Tag>& element, Factory&& factory);

    Index<Tag> insert_new_with_hash(size_t h, const Data<Tag>& element);

    std::pair<Index<Tag>, bool> insert(const Data<Tag>& element) { return insert_with_hash(IndexedHashSet::hash(element), element); }

    const Data<Tag>& operator[](Index<Tag> index) const noexcept { return m_rows[uint_t(index)]; }

    const Data<Tag>& at(Index<Tag> index) const { return m_rows.at(uint_t(index)); }

    const Data<Tag>& front() const { return m_rows.front(); }

    const Data<Tag>& back() const { return m_rows.back(); }

    size_t memory_usage() const noexcept;

    size_t size() const noexcept { return m_rows.size(); }

    bool empty() const noexcept { return m_rows.empty(); }

private:
    Store* m_store;  ///< Owned. std::unique_ptr would instantiate its tuple machinery for every element type.
    Rows m_rows;     ///< Reads the elements that m_store owns.
};
}  // namespace ygg

#endif
