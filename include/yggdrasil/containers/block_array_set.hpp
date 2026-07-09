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

#ifndef YGG_CONTAINERS_BLOCK_ARRAY_SET_HPP_
#define YGG_CONTAINERS_BLOCK_ARRAY_SET_HPP_

#include "yggdrasil/containers/block_array_pool.hpp"
#include "yggdrasil/core/concepts.hpp"
#include "yggdrasil/core/config.hpp"
#include "yggdrasil/semantics/equal_to.hpp"
#include "yggdrasil/semantics/hash.hpp"

#include <algorithm>
#include <cassert>
#include <concepts>
#include <gtl/phmap.hpp>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>

namespace ygg
{

template<std::unsigned_integral Block, typename Coder = bit::ForwardingBlockCoder<Block>, size_t FirstSegmentSize = 16>
class BlockArraySet
{
private:
    using pool_type = BlockArrayPool<Block, Coder, FirstSegmentSize>;

public:
    using value_type = typename pool_type::value_type;
    using index_type = uint_t;
    using ConstArrayView = typename pool_type::ConstArrayView;

private:
    struct Hash;
    struct EqualTo;

    class IndexableHash;
    class IndexableEqualTo;

    void ensure_fits(std::span<const value_type> element) const
    {
        if (element.size() != length())
            throw std::invalid_argument("BlockArraySet: wrong number of elements.");
    }

    index_type insert_new_with_matching_hash(size_t h, std::span<const value_type> element)
    {
        const auto index = to_uint_t(m_pool->size());
        m_pool->push_back(element);

        [[maybe_unused]] const auto [it, inserted] = m_set.emplace_with_hash(h, index);
        assert(inserted);

        return index;
    }

public:
    explicit BlockArraySet(size_t length) : m_pool(std::make_unique<pool_type>(length)), m_set(0, IndexableHash(*m_pool), IndexableEqualTo(*m_pool)) {}

    void clear() noexcept
    {
        m_set.clear();
        m_pool->clear();
    }

    static size_t hash(std::span<const value_type> element) noexcept { return gtl::phmap_mix<sizeof(size_t)>()(Hash {}(element)); }

    std::optional<index_type> find_with_hash(std::span<const value_type> element, size_t h) const
    {
        ensure_fits(element);
        assert(h == BlockArraySet::hash(element) && "The given hash does not match container internal's hash.");
        assert(h == m_set.hash(element));

        const auto it = m_set.find(element, h);
        if (it != m_set.end())
            return *it;

        return std::nullopt;
    }

    std::optional<index_type> find(std::span<const value_type> element) const { return find_with_hash(element, BlockArraySet::hash(element)); }

    bool contains_with_hash(std::span<const value_type> element, size_t h) const { return find_with_hash(element, h).has_value(); }

    std::pair<index_type, bool> insert_with_hash(size_t h, std::span<const value_type> element)
    {
        ensure_fits(element);
        assert(h == BlockArraySet::hash(element) && "The given hash does not match container internal's hash.");
        assert(h == m_set.hash(element));

        if (const auto it = m_set.find(element, h); it != m_set.end())
            return { *it, false };

        return { insert_new_with_matching_hash(h, element), true };
    }

    index_type insert_new_with_hash(size_t h, std::span<const value_type> element)
    {
        ensure_fits(element);
        assert(h == BlockArraySet::hash(element) && "The given hash does not match container internal's hash.");
        assert(h == m_set.hash(element));

        return insert_new_with_matching_hash(h, element);
    }

    std::pair<index_type, bool> insert(std::span<const value_type> element) { return insert_with_hash(BlockArraySet::hash(element), element); }

    bool contains(std::span<const value_type> element) const
    {
        ensure_fits(element);
        return m_set.contains(element);
    }

    ConstArrayView operator[](index_type index) const { return std::as_const(*m_pool)[index]; }

    ConstArrayView at(index_type index) const { return std::as_const(*m_pool).at(index); }

    ConstArrayView front() const
    {
        ensure_not_empty();
        return (*this)[0];
    }

    ConstArrayView back() const
    {
        ensure_not_empty();
        return (*this)[size() - 1];
    }

    size_t size() const noexcept { return m_pool->size(); }
    size_t capacity() const noexcept { return m_pool->capacity(); }
    bool empty() const noexcept { return m_pool->empty(); }
    size_t length() const noexcept { return m_pool->length(); }
    const auto& segments() const noexcept { return m_pool->segments(); }

private:
    void ensure_not_empty() const
    {
        if (empty())
            throw std::out_of_range("BlockArraySet: container is empty.");
    }

    struct Hash
    {
        template<InputRangeOf<value_type> Range>
        hash_t operator()(const Range& el) const noexcept
        {
            return ygg::hash_range(el);
        }
    };

    struct EqualTo
    {
        template<InputRangeOf<value_type> Range1, InputRangeOf<value_type> Range2>
        bool operator()(const Range1& lhs, const Range2& rhs) const noexcept
        {
            return equal_range(lhs, rhs);
        }
    };

    class IndexableHash
    {
    private:
        const pool_type* pool = nullptr;
        Hash m_hash;

    public:
        using is_transparent = void;

        IndexableHash() noexcept = default;
        explicit IndexableHash(const pool_type& pool_) noexcept : pool(&pool_), m_hash() {}

        size_t operator()(index_type idx) const noexcept { return m_hash((*pool)[idx]); }
        size_t operator()(std::span<const value_type> values) const noexcept { return m_hash(values); }
    };

    class IndexableEqualTo
    {
    private:
        const pool_type* pool = nullptr;
        EqualTo m_equal_to;

    public:
        using is_transparent = void;

        IndexableEqualTo() noexcept = default;
        explicit IndexableEqualTo(const pool_type& pool_) noexcept : pool(&pool_), m_equal_to() {}

        bool operator()(index_type lhs, index_type rhs) const noexcept { return m_equal_to((*pool)[lhs], (*pool)[rhs]); }
        bool operator()(std::span<const value_type> lhs, index_type rhs) const noexcept { return m_equal_to(lhs, (*pool)[rhs]); }
        bool operator()(index_type lhs, std::span<const value_type> rhs) const noexcept { return m_equal_to((*pool)[lhs], rhs); }
    };

    std::unique_ptr<pool_type> m_pool;
    gtl::flat_hash_set<index_type, IndexableHash, IndexableEqualTo> m_set;
};

}  // namespace ygg

#endif
