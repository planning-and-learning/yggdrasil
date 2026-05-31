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

#include "cista/serialization.h"
#include "yggdrasil/buffer/declarations.hpp"
#include "yggdrasil/buffer/segmented_buffer.hpp"
#include <yggdrasil/semantics/canonicalization.hpp>
#include <yggdrasil/serialization/cista_comparators.hpp>
#include <yggdrasil/serialization/cista_equal_to.hpp>
#include <yggdrasil/serialization/cista_hash.hpp>
#include <yggdrasil/core/config.hpp>
#include <yggdrasil/containers/segmented_vector.hpp>
#include <yggdrasil/core/types.hpp>
#include <yggdrasil/semantics/equal_to.hpp>
#include <yggdrasil/semantics/hash.hpp>

#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <gtl/phmap.hpp>
#include <optional>
#include <utility>
#include <vector>

namespace ygg::buffer
{
using ygg::CanonicalDataTag;
using ygg::Data;
using ygg::EqualToFor;
using ygg::HashFor;
using ygg::Index;
using ygg::is_canonical;

template<CanonicalDataTag Tag, HashFor<Data<Tag>> H = ygg::Hash<Data<Tag>>, EqualToFor<Data<Tag>> E = ygg::EqualTo<Data<Tag>>>
class IndexedHashSet
{
private:
    class IndexableHash;
    class IndexableEqualTo;

    using VectorType = std::vector<const Data<Tag>*>;

public:
    IndexedHashSet() = default;
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

    /**
     * Capacity
     */

    bool empty() const noexcept { return m_storage->empty(); }
    size_t size() const noexcept { return m_storage->size(); }

    /**
     * Modifiers
     */

    void clear()
    {
        m_set.clear();
        m_storage->clear();
    }

    static size_t hash(const Data<Tag>& element) noexcept { return gtl::phmap_mix<sizeof(size_t)>()(H {}(element)); }

    std::optional<Index<Tag>> find_with_hash(const Data<Tag>& element, size_t h) const noexcept
    {
        assert(is_canonical(element) && "The given element is not canonical. Did you forget to call canonicalize?");
        assert(h == hash(element) && "The given hash does not match container internal's hash.");
        assert(h == m_set.hash(element));

        const auto it = m_set.find(element, h);
        if (it != m_set.end())
            return *it;

        return std::nullopt;
    }

    std::optional<Index<Tag>> find(const Data<Tag>& element) const noexcept
    {
        assert(is_canonical(element));
        assert(IndexedHashSet::hash(element) == m_set.hash(element));

        return find_with_hash(element, IndexedHashSet::hash(element));
    }

    template<::cista::mode Mode = ygg::CISTA_MODE>
    std::pair<Index<Tag>, bool> insert_with_hash(size_t h, const Data<Tag>& element)
    {
        assert(is_canonical(element) && "The given element is not canonical. Did you forget to call canonicalize?");
        assert(h == IndexedHashSet::hash(element) && "The given hash does not match container internal's hash.");
        assert(h == m_set.hash(element));

        // 1. Check if element already exists

        auto it = m_set.find(element, h);
        if (it != m_set.end())
            return std::make_pair(*it, false);

        return std::make_pair(insert_new_with_hash<Mode>(h, element), true);
    }

    template<::cista::mode Mode = ygg::CISTA_MODE>
    Index<Tag> insert_new_with_hash(size_t h, const Data<Tag>& element)
    {
        assert(is_canonical(element) && "The given element is not canonical. Did you forget to call canonicalize?");
        assert(h == IndexedHashSet::hash(element) && "The given hash does not match container internal's hash.");
        assert(h == m_set.hash(element));

        // 2. Serialize
        m_buf->reset();
        ::cista::serialize<Mode>(*m_buf, element);

        // 3. Write to storage
        auto begin = m_arena->write(m_buf->base(), m_buf->size(), alignof(Data<Tag>));

        const auto index = Index<Tag>(ygg::to_uint_t(m_storage->size()));

        // 4. Insert into vec
        const auto serialized_element = ::cista::deserialize<const Data<Tag>, Mode>(begin, begin + m_buf->size());
        m_storage->push_back(serialized_element);

        // 5. Insert into set
        [[maybe_unused]] auto [it2, inserted] = m_set.emplace_with_hash(h, index);
        assert(inserted);

        return index;
    }

    // const T* always points to a valid instantiation of the class.
    // We return const T* here to avoid bugs when using structured bindings.
    template<::cista::mode Mode = ygg::CISTA_MODE>
    std::pair<Index<Tag>, bool> insert(const Data<Tag>& element)
    {
        assert(is_canonical(element));
        assert(IndexedHashSet::hash(element) == m_set.hash(element));

        return insert_with_hash<Mode>(IndexedHashSet::hash(element), element);
    }

    /**
     * Lookup
     */

    const Data<Tag>& operator[](Index<Tag> index) const noexcept
    {
        assert(index.get_value() < m_storage->size());
        return *(*m_storage)[index.get_value()];
    }

    const Data<Tag>& front() const
    {
        assert(!m_storage->empty());
        return *m_storage->front();
    }

private:
    class IndexableHash
    {
    private:
        const VectorType* m_storage;
        H m_hash;

    public:
        using is_transparent = void;

        IndexableHash() noexcept : m_storage(nullptr) {}
        explicit IndexableHash(const VectorType& storage) noexcept : m_storage(&storage) {}

        size_t operator()(Index<Tag> el) const noexcept { return m_hash(*(*m_storage)[ygg::uint_t(el)]); }
        size_t operator()(const Data<Tag>& el) const noexcept { return m_hash(el); }
    };

    class IndexableEqualTo
    {
    private:
        const VectorType* m_storage;
        E m_equal_to;

    public:
        using is_transparent = void;

        IndexableEqualTo() noexcept : m_storage(nullptr), m_equal_to() {}
        explicit IndexableEqualTo(const VectorType& storage) noexcept : m_storage(&storage), m_equal_to() {}

        bool operator()(Index<Tag> lhs, Index<Tag> rhs) const noexcept { return m_equal_to(*(*m_storage)[ygg::uint_t(lhs)], *(*m_storage)[ygg::uint_t(rhs)]); }
        bool operator()(const Data<Tag>& lhs, Index<Tag> rhs) const noexcept { return m_equal_to(lhs, *(*m_storage)[ygg::uint_t(rhs)]); }
        bool operator()(Index<Tag> lhs, const Data<Tag>& rhs) const noexcept { return m_equal_to(*(*m_storage)[ygg::uint_t(lhs)], rhs); }
        bool operator()(const Data<Tag>& lhs, const Data<Tag>& rhs) const noexcept { return m_equal_to(lhs, rhs); }
    };

    std::unique_ptr<VectorType> m_storage;
    gtl::flat_hash_set<Index<Tag>, IndexableHash, IndexableEqualTo> m_set;

    ::cista::buf<std::vector<uint8_t>>* m_buf;
    SegmentedBuffer* m_arena;
};

}

#endif
