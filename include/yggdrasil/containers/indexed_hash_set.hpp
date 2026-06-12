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

#ifndef YGG_COMMON_INDEXED_HASH_SET_HPP_
#define YGG_COMMON_INDEXED_HASH_SET_HPP_

#include "yggdrasil/containers/segmented_vector.hpp"
#include "yggdrasil/core/bit.hpp"
#include "yggdrasil/core/config.hpp"
#include "yggdrasil/core/types.hpp"
#include "yggdrasil/ids/index_mixins.hpp"
#include "yggdrasil/semantics/equal_to.hpp"
#include "yggdrasil/semantics/hash.hpp"

#include <cassert>
#include <concepts>
#include <cstddef>
#include <gtl/phmap.hpp>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ygg
{

template<typename Tag, HashFor<Data<Tag>> H = Hash<Data<Tag>>, EqualToFor<Data<Tag>> E = EqualTo<Data<Tag>>, size_t FirstSegmentSize = 32>
class IndexedHashSet
{
    static_assert(bit::is_power_of_two(FirstSegmentSize));

private:
    class IndexableHash;
    class IndexableEqualTo;

    using VectorType = SegmentedVector<Data<Tag>, FirstSegmentSize>;

public:
    IndexedHashSet() : m_storage(std::make_unique<VectorType>()), m_set(0, IndexableHash(*m_storage), IndexableEqualTo(*m_storage)) {}
    IndexedHashSet(const IndexedHashSet& other) = delete;
    IndexedHashSet& operator=(const IndexedHashSet& other) = delete;
    IndexedHashSet(IndexedHashSet&& other) = default;
    IndexedHashSet& operator=(IndexedHashSet&& other) = default;

    void clear() noexcept
    {
        m_set.clear();
        m_storage->clear();
    }

    static size_t hash(const Data<Tag>& element) noexcept { return gtl::phmap_mix<sizeof(size_t)>()(H {}(element)); }

    std::optional<Index<Tag>> find_with_hash(const Data<Tag>& element, size_t h) const
    {
        assert(h == IndexedHashSet::hash(element) && "The given hash does not match container internal's hash.");
        assert(h == m_set.hash(element));

        if (auto it = m_set.find(element, h); it != m_set.end())
            return *it;

        return std::nullopt;
    }

    std::optional<Index<Tag>> find(const Data<Tag>& element) const
    {
        assert(IndexedHashSet::hash(element) == m_set.hash(element));

        return find_with_hash(element, IndexedHashSet::hash(element));
    }

    bool contains_with_hash(const Data<Tag>& element, size_t h) const { return find_with_hash(element, h).has_value(); }

    bool contains(const Data<Tag>& element) const { return find(element).has_value(); }

    std::pair<Index<Tag>, bool> insert_with_hash(size_t h, const Data<Tag>& element)
    {
        assert(h == hash(element) && "The given hash does not match container internal's hash.");
        assert(h == m_set.hash(element));

        if (auto it = m_set.find(element, h); it != m_set.end())
            return { *it, false };

        return { insert_new_with_hash(h, element), true };
    }

    Index<Tag> insert_new_with_hash(size_t h, const Data<Tag>& element)
    {
        assert(h == hash(element) && "The given hash does not match container internal's hash.");
        assert(h == m_set.hash(element));

        Index<Tag> idx(to_uint_t(m_storage->size()));
        m_storage->push_back(element);
        [[maybe_unused]] const auto [it, inserted] = m_set.emplace_with_hash(h, idx);
        assert(inserted);
        return idx;
    }

    std::pair<Index<Tag>, bool> insert(const Data<Tag>& element)
    {
        assert(IndexedHashSet::hash(element) == m_set.hash(element));

        return insert_with_hash(IndexedHashSet::hash(element), element);
    }

    const Data<Tag>& operator[](Index<Tag> idx) const noexcept { return (*m_storage)[uint_t(idx)]; }

    const Data<Tag>& at(Index<Tag> idx) const { return m_storage->at(uint_t(idx)); }

    const Data<Tag>& front() const noexcept { return m_storage->front(); }

    size_t memory_usage() const noexcept
    {
        size_t bytes = 0;
        bytes += m_storage ? m_storage->memory_usage() : 0;
        bytes += m_set.capacity() * (sizeof(Index<Tag>) + sizeof(gtl::priv::ctrl_t));
        return bytes;
    }

    std::size_t size() const noexcept { return m_storage->size(); }

    bool empty() const noexcept { return m_storage->empty(); }

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

        size_t operator()(Index<Tag> el) const noexcept { return m_hash((*m_storage)[uint_t(el)]); }
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

        bool operator()(Index<Tag> lhs, Index<Tag> rhs) const noexcept { return m_equal_to((*m_storage)[uint_t(lhs)], (*m_storage)[uint_t(rhs)]); }
        bool operator()(const Data<Tag>& lhs, Index<Tag> rhs) const noexcept { return m_equal_to(lhs, (*m_storage)[uint_t(rhs)]); }
        bool operator()(Index<Tag> lhs, const Data<Tag>& rhs) const noexcept { return m_equal_to((*m_storage)[uint_t(lhs)], rhs); }
        bool operator()(const Data<Tag>& lhs, const Data<Tag>& rhs) const noexcept { return m_equal_to(lhs, rhs); }
    };

    std::unique_ptr<VectorType> m_storage;
    gtl::flat_hash_set<Index<Tag>, IndexableHash, IndexableEqualTo> m_set;
};
}  // namespace ygg

#endif
