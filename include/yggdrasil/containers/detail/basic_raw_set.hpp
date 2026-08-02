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

#ifndef YGG_CONTAINERS_DETAIL_BASIC_RAW_SET_HPP_
#define YGG_CONTAINERS_DETAIL_BASIC_RAW_SET_HPP_

#include "yggdrasil/containers/detail/concurrency.hpp"
#include "yggdrasil/core/config.hpp"
#include "yggdrasil/semantics/equal_to.hpp"
#include "yggdrasil/semantics/hash.hpp"

#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

namespace ygg::detail
{

template<typename Pool>
class BasicRawSet
{
private:
    using pool_type = Pool;
    static constexpr bool ThreadSafe = pool_type::thread_safe;

public:
    using value_type = typename pool_type::value_type;
    using index_type = uint_t;
    using ConstView = typename pool_type::ConstView;

private:
    struct IndexableHash
    {
        using is_transparent = void;

        const pool_type* pool = nullptr;

        IndexableHash() noexcept = default;
        explicit IndexableHash(const pool_type& pool_) noexcept : pool(&pool_) {}

        size_t operator()(index_type index) const noexcept { return ygg::hash_range((*pool)[index]); }
        size_t operator()(std::span<const value_type> value) const noexcept { return ygg::hash_range(value); }
    };

    struct IndexableEqualTo
    {
        using is_transparent = void;

        const pool_type* pool = nullptr;

        IndexableEqualTo() noexcept = default;
        explicit IndexableEqualTo(const pool_type& pool_) noexcept : pool(&pool_) {}

        bool operator()(index_type lhs, index_type rhs) const noexcept { return equal_range((*pool)[lhs], (*pool)[rhs]); }
        bool operator()(std::span<const value_type> lhs, index_type rhs) const noexcept { return equal_range(lhs, (*pool)[rhs]); }
        bool operator()(index_type lhs, std::span<const value_type> rhs) const noexcept { return equal_range((*pool)[lhs], rhs); }
        bool operator()(std::span<const value_type> lhs, std::span<const value_type> rhs) const noexcept { return equal_range(lhs, rhs); }
    };

    using SetType = HashSetType<index_type, IndexableHash, IndexableEqualTo, ThreadSafe>;

    void ensure_fits(std::span<const value_type> value) const
    {
        if constexpr (requires(const pool_type& pool) { pool.array_size(); })
        {
            if (value.size() != m_pool->array_size())
                throw std::invalid_argument("BasicRawSet: wrong number of elements.");
        }
    }

protected:
    explicit BasicRawSet(std::unique_ptr<pool_type> pool) : m_pool(std::move(pool)), m_set(0, IndexableHash(*m_pool), IndexableEqualTo(*m_pool)) {}

    const pool_type& storage() const noexcept { return *m_pool; }

public:
    static constexpr bool thread_safe = ThreadSafe;

    BasicRawSet(const BasicRawSet&) = delete;
    BasicRawSet& operator=(const BasicRawSet&) = delete;
    BasicRawSet(BasicRawSet&&) = default;
    BasicRawSet& operator=(BasicRawSet&&) = default;

    std::optional<index_type> find(std::span<const value_type> value) const
    {
        ensure_fits(value);
        const auto hash = m_set.hash(value);
        return find_value_with_hash<ThreadSafe>(m_set, value, hash);
    }

    bool contains(std::span<const value_type> value) const { return find(value).has_value(); }

    index_type insert(std::span<const value_type> value)
    {
        ensure_fits(value);
        const auto hash = m_set.hash(value);
        return find_or_lazy_insert_value_with_hash<ThreadSafe>(m_set, value, hash, [&] { return m_pool->insert(value); }).first;
    }

    ConstView operator[](index_type index) const noexcept { return (*m_pool)[index]; }
    ConstView at(index_type index) const { return m_pool->at(index); }
    ConstView front() const { return m_pool->front(); }
    ConstView back() const { return m_pool->back(); }

    size_t size() const noexcept { return m_pool->size(); }
    bool empty() const noexcept { return m_pool->empty(); }
    size_t memory_usage() const noexcept { return m_pool->memory_usage() + hash_set_memory_usage(m_set); }

    void clear() noexcept
    {
        m_set.clear();
        m_pool->clear();
    }

private:
    std::unique_ptr<pool_type> m_pool;
    SetType m_set;
};

}  // namespace ygg::detail

#endif
