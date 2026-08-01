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
#include "yggdrasil/containers/detail/concurrency.hpp"
#include "yggdrasil/containers/segmented_vector.hpp"
#include "yggdrasil/core/config.hpp"
#include "yggdrasil/core/types.hpp"
#include "yggdrasil/semantics/equal_to.hpp"
#include "yggdrasil/semantics/hash.hpp"
#include "yggdrasil/serialization/cista_comparators.hpp"
#include "yggdrasil/serialization/cista_equal_to.hpp"
#include "yggdrasil/serialization/cista_hash.hpp"

#include <cassert>
#include <cista/serialization.h>
#include <cstddef>
#include <functional>
#include <gtl/phmap.hpp>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace ygg::buffer
{
/// ThreadSafe permits concurrent lookup, insertion, size queries, and reads of
/// published indices. Hash-table locking is limited to the target shard and
/// the append lock covers only index allocation, serialization, and storage
/// publication. Clear, memory inspection, move, and destruction require
/// quiescence. Each ThreadSafe instance requires exclusive use of its supplied
/// serialization buffer and arena.
template<typename Tag,
         HashFor<Data<Tag>> H = Hash<Data<Tag>>,
         EqualToFor<Data<Tag>> E = EqualTo<Data<Tag>>,
         size_t FirstSegmentSize = 32,
         bool ThreadSafe = false>
class IndexedHashSet
{
private:
    class IndexableHash;
    class IndexableEqualTo;

    using VectorType = SegmentedVector<const Data<Tag>*, FirstSegmentSize, ThreadSafe>;
    using SetType = ygg::detail::HashSetType<Index<Tag>, IndexableHash, IndexableEqualTo, ThreadSafe>;

    template<::cista::mode Mode>
    Index<Tag> append_new_unlocked(const Data<Tag>& element)
    {
        if (!m_buf || !m_arena)
            throw std::logic_error("Buffer IndexedHashSet requires a buffer and "
                                   "arena before insertion.");

        const auto index = Index<Tag>(to_uint_t(m_storage->size()));
        m_buf->reset();
        ::cista::serialize<Mode>(*m_buf, element);
        auto begin = m_arena->write(m_buf->base(), m_buf->size(), alignof(Data<Tag>));
        const auto serialized_element = ::cista::deserialize<const Data<Tag>, Mode>(begin, begin + m_buf->size());
        m_storage->push_back(serialized_element);
        return index;
    }

    template<::cista::mode Mode>
    Index<Tag> append_new(const Data<Tag>& element)
    {
        return ygg::detail::with_lock<ThreadSafe>(m_append_mutex, [&] { return append_new_unlocked<Mode>(element); });
    }

public:
    static constexpr bool thread_safe = ThreadSafe;

    IndexedHashSet() :
        m_storage(std::make_unique<VectorType>()),
        m_set(0, IndexableHash(*m_storage), IndexableEqualTo(*m_storage)),
        m_buf(nullptr),
        m_arena(nullptr)
    {
    }
    IndexedHashSet(::cista::buf<std::vector<uint8_t>>& buf, SegmentedBuffer& arena) :
        m_storage(std::make_unique<VectorType>()),
        m_set(0, IndexableHash(*m_storage), IndexableEqualTo(*m_storage)),
        m_buf(&buf),
        m_arena(&arena)
    {
    }
    IndexedHashSet(const IndexedHashSet& other) = delete;
    IndexedHashSet& operator=(const IndexedHashSet& other) = delete;
    IndexedHashSet(IndexedHashSet&& other) = default;
    IndexedHashSet& operator=(IndexedHashSet&& other) = default;

    void clear() noexcept
    {
        m_set.clear();
        m_storage->clear();
    }

    static size_t hash(const Data<Tag>& element) noexcept(std::is_nothrow_default_constructible_v<H> && std::is_nothrow_invocable_v<const H&, const Data<Tag>&>)
    {
        return gtl::phmap_mix<sizeof(size_t)>()(H {}(element));
    }

    std::optional<Index<Tag>> find_with_hash(const Data<Tag>& element, size_t h) const
    {
        assert(h == hash(element) && "The given hash does not match container internal's hash.");
        assert(h == m_set.hash(element));

        return ygg::detail::find_value_with_hash<ThreadSafe>(m_set, element, h);
    }

    std::optional<Index<Tag>> find(const Data<Tag>& element) const { return find_with_hash(element, IndexedHashSet::hash(element)); }

    bool contains_with_hash(const Data<Tag>& element, size_t h) const { return find_with_hash(element, h).has_value(); }

    bool contains(const Data<Tag>& element) const { return find(element).has_value(); }

    template<::cista::mode Mode = CISTA_MODE>
    std::pair<Index<Tag>, bool> insert_with_hash(size_t h, const Data<Tag>& element)
    {
        assert(h == IndexedHashSet::hash(element) && "The given hash does not match container internal's hash.");
        assert(h == m_set.hash(element));

        return ygg::detail::find_or_lazy_insert_value_with_hash<ThreadSafe>(m_set, element, h, [&] { return append_new<Mode>(element); });
    }

    /// Rechecks a caller-observed miss and returns the canonical stored index.
    template<::cista::mode Mode = CISTA_MODE>
    std::pair<Index<Tag>, bool> complete_miss_with_hash(size_t h, const Data<Tag>& element)
    {
        assert(h == IndexedHashSet::hash(element) && "The given hash does not match container internal's hash.");
        assert(h == m_set.hash(element));

        return ygg::detail::complete_miss_value_with_hash<ThreadSafe>(m_set, element, h, [&] { return append_new<Mode>(element); });
    }

    template<::cista::mode Mode = CISTA_MODE>
    Index<Tag> insert_new_with_hash(size_t h, const Data<Tag>& element)
    {
        const auto [index, inserted] = complete_miss_with_hash<Mode>(h, element);
        if (!inserted)
            throw std::logic_error("buffer::IndexedHashSet::insert_new_with_hash requires an absent key.");
        return index;
    }

    template<::cista::mode Mode = CISTA_MODE>
    std::pair<Index<Tag>, bool> insert(const Data<Tag>& element)
    {
        return insert_with_hash<Mode>(IndexedHashSet::hash(element), element);
    }

    const Data<Tag>& operator[](Index<Tag> index) const noexcept
    {
        assert(index.get_value() < m_storage->size());
        return *(*m_storage)[index.get_value()];
    }

    const Data<Tag>& at(Index<Tag> index) const
    {
        if (index.get_value() >= m_storage->size())
            throw std::out_of_range("buffer::IndexedHashSet: index out of range.");
        return (*this)[index];
    }

    const Data<Tag>& front() const
    {
        if (m_storage->empty())
            throw std::out_of_range("buffer::IndexedHashSet: index out of range.");
        return *m_storage->front();
    }

    const Data<Tag>& back() const
    {
        if (m_storage->empty())
            throw std::out_of_range("buffer::IndexedHashSet: index out of range.");
        return *m_storage->back();
    }

    size_t memory_usage() const noexcept
    {
        size_t bytes = 0;
        bytes += m_storage ? m_storage->memory_usage() : 0;
        bytes += ygg::detail::hash_set_memory_usage(m_set);
        return bytes;
    }

    size_t size() const noexcept { return m_storage->size(); }

    bool empty() const noexcept { return m_storage->empty(); }

private:
    class IndexableHash
    {
    private:
        const VectorType* m_storage;
        H m_hash;

    public:
        using is_transparent = void;

        IndexableHash() noexcept(std::is_nothrow_default_constructible_v<H>) : m_storage(nullptr) {}
        explicit IndexableHash(const VectorType& storage) noexcept(std::is_nothrow_default_constructible_v<H>) : m_storage(&storage) {}

        size_t operator()(Index<Tag> el) const noexcept(std::is_nothrow_invocable_v<const H&, const Data<Tag>&>) { return m_hash(*(*m_storage)[uint_t(el)]); }
        size_t operator()(const Data<Tag>& el) const noexcept(std::is_nothrow_invocable_v<const H&, const Data<Tag>&>) { return m_hash(el); }
    };

    class IndexableEqualTo
    {
    private:
        const VectorType* m_storage;
        E m_equal_to;

    public:
        using is_transparent = void;

        IndexableEqualTo() noexcept(std::is_nothrow_default_constructible_v<E>) : m_storage(nullptr), m_equal_to() {}
        explicit IndexableEqualTo(const VectorType& storage) noexcept(std::is_nothrow_default_constructible_v<E>) : m_storage(&storage), m_equal_to() {}

        bool operator()(Index<Tag> lhs, Index<Tag> rhs) const noexcept(std::is_nothrow_invocable_v<const E&, const Data<Tag>&, const Data<Tag>&>)
        {
            return m_equal_to(*(*m_storage)[uint_t(lhs)], *(*m_storage)[uint_t(rhs)]);
        }
        bool operator()(const Data<Tag>& lhs, Index<Tag> rhs) const noexcept(std::is_nothrow_invocable_v<const E&, const Data<Tag>&, const Data<Tag>&>)
        {
            return m_equal_to(lhs, *(*m_storage)[uint_t(rhs)]);
        }
        bool operator()(Index<Tag> lhs, const Data<Tag>& rhs) const noexcept(std::is_nothrow_invocable_v<const E&, const Data<Tag>&, const Data<Tag>&>)
        {
            return m_equal_to(*(*m_storage)[uint_t(lhs)], rhs);
        }
        bool operator()(const Data<Tag>& lhs, const Data<Tag>& rhs) const noexcept(std::is_nothrow_invocable_v<const E&, const Data<Tag>&, const Data<Tag>&>)
        {
            return m_equal_to(lhs, rhs);
        }
    };

    std::unique_ptr<VectorType> m_storage;
    SetType m_set;
    [[no_unique_address]] ygg::detail::Mutex<ThreadSafe> m_append_mutex;

    ::cista::buf<std::vector<uint8_t>>* m_buf;
    SegmentedBuffer* m_arena;
};

}  // namespace ygg::buffer

#endif
