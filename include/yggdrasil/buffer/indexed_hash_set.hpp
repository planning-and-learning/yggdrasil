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

#ifndef YGG_BUFFER_INDEXED_HASH_SET_HPP_
#define YGG_BUFFER_INDEXED_HASH_SET_HPP_

#include "yggdrasil/buffer/declarations.hpp"
#include "yggdrasil/buffer/segmented_buffer.hpp"
#include "yggdrasil/containers/segmented_vector.hpp"
#include "yggdrasil/core/config.hpp"
#include "yggdrasil/core/types.hpp"
#include "yggdrasil/semantics/equal_to.hpp"
#include "yggdrasil/semantics/hash.hpp"
#include "yggdrasil/serialization/cista_comparators.hpp"
#include "yggdrasil/serialization/cista_equal_to.hpp"
#include "yggdrasil/serialization/cista_hash.hpp"

#include <cassert>
#include <cista/mode.h>
#include <cstddef>
#include <gtl/phmap_utils.hpp>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace ygg::buffer
{
/// ThreadSafe permits concurrent normal lookup, insertion, size queries, and reads of
/// published indices. Hash-table locking is limited to the target shard and
/// the append lock covers index allocation, serialization, and storage
/// publication. Clear, memory inspection, move, and destruction require
/// quiescence. Each ThreadSafe instance requires exclusive use of its supplied
/// serialization buffer and arena. find_unsafe_with_hash additionally requires
/// the documented caller-enforced quiescence.
///
/// Reads by index are defined here. The value-to-index lookup, construction, and the write API (insertion and
/// serialization) are defined in detail/indexed_hash_set_impl.hpp, which needs cista/serialization.h; sources that
/// instantiate the set include it.
template<typename Tag,
         HashFor<Data<Tag>> H = Hash<Data<Tag>>,
         EqualToFor<Data<Tag>> E = EqualTo<Data<Tag>>,
         size_t FirstSegmentSize = 32,
         bool ThreadSafe = false>
class IndexedHashSet
{
private:
    class Lookup;

    using VectorType = SegmentedVector<const Data<Tag>*, FirstSegmentSize, ThreadSafe, uint_t>;

    template<::cista::mode Mode>
    const Data<Tag>* serialize(const Data<Tag>& element);

    template<::cista::mode Mode, typename Factory>
    Index<Tag> append_new_with_index(Factory&& factory);

    template<::cista::mode Mode>
    Index<Tag> append_new(const Data<Tag>& element);

public:
    static constexpr bool thread_safe = ThreadSafe;

    IndexedHashSet();
    IndexedHashSet(::cista::buf<std::vector<uint8_t>>& buf, SegmentedBuffer& arena);
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

    template<::cista::mode Mode = CISTA_MODE>
    std::pair<Index<Tag>, bool> insert_with_hash(size_t h, const Data<Tag>& element);

    /// Rechecks a caller-observed miss and returns the canonical stored index.
    template<::cista::mode Mode = CISTA_MODE>
    std::pair<Index<Tag>, bool> complete_miss_with_hash(size_t h, const Data<Tag>& element);

    /// Runs the factory inside the insertion transaction with the reserved local index.
    /// The result must preserve the key's identity, and the factory must not access this set.
    template<::cista::mode Mode = CISTA_MODE, typename Factory>
    std::pair<Index<Tag>, bool> complete_miss_with_hash(size_t h, const Data<Tag>& element, Factory&& factory);

    template<::cista::mode Mode = CISTA_MODE>
    Index<Tag> insert_new_with_hash(size_t h, const Data<Tag>& element);

    template<::cista::mode Mode = CISTA_MODE>
    std::pair<Index<Tag>, bool> insert(const Data<Tag>& element);

    const Data<Tag>& operator[](Index<Tag> index) const noexcept { return *m_storage[uint_t(index)]; }

    const Data<Tag>& at(Index<Tag> index) const { return *m_storage.at(uint_t(index)); }

    const Data<Tag>& front() const { return *m_storage.front(); }

    const Data<Tag>& back() const { return *m_storage.back(); }

    size_t memory_usage() const noexcept;

    size_t size() const noexcept { return m_storage.size(); }

    bool empty() const noexcept { return m_storage.empty(); }

private:
    VectorType m_storage;
    std::unique_ptr<Lookup> m_lookup;

    ::cista::buf<std::vector<uint8_t>>* m_buf;
    SegmentedBuffer* m_arena;
};

}  // namespace ygg::buffer

#endif
