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

#ifndef YGG_CONTAINERS_DETAIL_BASIC_ARRAY_SET_HPP_
#define YGG_CONTAINERS_DETAIL_BASIC_ARRAY_SET_HPP_

#include "yggdrasil/containers/detail/concurrency.hpp"
#include "yggdrasil/core/concepts.hpp"
#include "yggdrasil/core/config.hpp"
#include "yggdrasil/semantics/equal_to.hpp"
#include "yggdrasil/semantics/hash.hpp"

#include <cassert>
#include <gtl/phmap.hpp>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

namespace ygg::detail
{

template<typename Pool>
class BasicArraySet
{
private:
    using pool_type = Pool;
    static constexpr bool ThreadSafe = pool_type::thread_safe;

public:
    using value_type = typename pool_type::value_type;
    using index_type = uint_t;
    using ConstArrayView = typename pool_type::ConstArrayView;

private:
    struct Hash
    {
        template<InputRangeOf<value_type> Range>
        hash_t operator()(const Range& element) const noexcept
        {
            return ygg::hash_range(element);
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
        const pool_type* m_pool = nullptr;
        Hash m_hash;

    public:
        using is_transparent = void;

        IndexableHash() noexcept = default;
        explicit IndexableHash(const pool_type& pool) noexcept : m_pool(&pool), m_hash() {}

        size_t operator()(index_type index) const noexcept { return m_hash((*m_pool)[index]); }
        size_t operator()(std::span<const value_type> values) const noexcept { return m_hash(values); }
    };

    class IndexableEqualTo
    {
    private:
        const pool_type* m_pool = nullptr;
        EqualTo m_equal_to;

    public:
        using is_transparent = void;

        IndexableEqualTo() noexcept = default;
        explicit IndexableEqualTo(const pool_type& pool) noexcept : m_pool(&pool), m_equal_to() {}

        bool operator()(index_type lhs, index_type rhs) const noexcept { return m_equal_to((*m_pool)[lhs], (*m_pool)[rhs]); }
        bool operator()(std::span<const value_type> lhs, index_type rhs) const noexcept { return m_equal_to(lhs, (*m_pool)[rhs]); }
        bool operator()(index_type lhs, std::span<const value_type> rhs) const noexcept { return m_equal_to((*m_pool)[lhs], rhs); }
    };

    using SetType = HashSetType<index_type, IndexableHash, IndexableEqualTo, ThreadSafe>;

    void ensure_fits(std::span<const value_type> element) const
    {
        if (element.size() != length())
            throw std::invalid_argument("ArraySet: wrong number of elements.");
    }

    void ensure_not_empty() const
    {
        if (empty())
            throw std::out_of_range("ArraySet: container is empty.");
    }

    index_type append_new(std::span<const value_type> element)
    {
        if constexpr (ThreadSafe)
        {
            // Check the bound before publishing so this cast cannot throw.
            return static_cast<index_type>(m_pool->push_back_bounded(element, std::numeric_limits<index_type>::max()));
        }
        else
        {
            const auto index = to_uint_t(m_pool->size());
            m_pool->push_back(element);
            return index;
        }
    }

protected:
    explicit BasicArraySet(std::unique_ptr<pool_type> pool) : m_pool(std::move(pool)), m_set(0, IndexableHash(*m_pool), IndexableEqualTo(*m_pool)) {}

    const pool_type& storage() const noexcept { return *m_pool; }

public:
    static constexpr bool thread_safe = ThreadSafe;

    void clear() noexcept
    {
        m_set.clear();
        m_pool->clear();
    }

    static size_t hash(std::span<const value_type> element) noexcept { return gtl::phmap_mix<sizeof(size_t)>()(Hash {}(element)); }

    std::optional<index_type> find_with_hash(std::span<const value_type> element, size_t h) const
    {
        ensure_fits(element);
        assert(h == hash(element) && "The given hash does not match container internal's hash.");
        assert(h == m_set.hash(element));

        return find_value_with_hash<ThreadSafe>(m_set, element, h);
    }

    std::optional<index_type> find(std::span<const value_type> element) const { return find_with_hash(element, hash(element)); }

    bool contains_with_hash(std::span<const value_type> element, size_t h) const { return find_with_hash(element, h).has_value(); }

    std::pair<index_type, bool> insert_with_hash(size_t h, std::span<const value_type> element)
    {
        ensure_fits(element);
        assert(h == hash(element) && "The given hash does not match container internal's hash.");
        assert(h == m_set.hash(element));

        return find_or_lazy_insert_value_with_hash<ThreadSafe>(m_set, element, h, [&] { return append_new(element); });
    }

    /// Rechecks a caller-observed miss and returns the canonical stored index.
    std::pair<index_type, bool> complete_miss_with_hash(size_t h, std::span<const value_type> element)
    {
        ensure_fits(element);
        assert(h == hash(element) && "The given hash does not match container internal's hash.");
        assert(h == m_set.hash(element));

        return complete_miss_value_with_hash<ThreadSafe>(m_set, element, h, [&] { return append_new(element); });
    }

    index_type insert_new_with_hash(size_t h, std::span<const value_type> element)
    {
        const auto [index, inserted] = complete_miss_with_hash(h, element);
        if (!inserted)
            throw std::logic_error("ArraySet::insert_new_with_hash requires an absent key.");
        return index;
    }

    std::pair<index_type, bool> insert(std::span<const value_type> element) { return insert_with_hash(hash(element), element); }

    bool contains(std::span<const value_type> element) const
    {
        if constexpr (ThreadSafe)
            return find(element).has_value();
        else
        {
            ensure_fits(element);
            return m_set.contains(element);
        }
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
    size_t memory_usage() const noexcept { return m_pool->memory_usage() + hash_set_memory_usage(m_set); }
    const auto& segments() const noexcept { return m_pool->segments(); }

private:
    std::unique_ptr<pool_type> m_pool;
    SetType m_set;
};

}  // namespace ygg::detail

#endif
