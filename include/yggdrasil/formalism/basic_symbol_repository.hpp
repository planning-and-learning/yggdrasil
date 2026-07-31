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

#ifndef YGG_FORMALISM_BASIC_SYMBOL_REPOSITORY_HPP_
#define YGG_FORMALISM_BASIC_SYMBOL_REPOSITORY_HPP_

#include <cassert>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <yggdrasil/buffer/declarations.hpp>
#include <yggdrasil/buffer/indexed_hash_set.hpp>
#include <yggdrasil/buffer/segmented_buffer.hpp>
#include <yggdrasil/containers/indexed_hash_set.hpp>
#include <yggdrasil/containers/tuple.hpp>
#include <yggdrasil/core/types.hpp>
#include <yggdrasil/formalism/declarations.hpp>
#include <yggdrasil/semantics/equal_to.hpp>
#include <yggdrasil/semantics/hash.hpp>

namespace ygg::formalism
{

template<typename T>
class BasicSymbolRepository
{
private:
    template<typename U, bool Trivial = uses_trivial_storage_v<U>>
    struct Slot;

    template<typename U>
    struct Slot<U, true>
    {
        ::ygg::IndexedHashSet<U> container;
        size_t parent_size = 0;

        static size_t hash(const Data<U>& builder) noexcept { return ::ygg::IndexedHashSet<U>::hash(builder); }

        void clear() noexcept { container.clear(); }
        size_t memory_usage() const noexcept { return container.memory_usage(); }
    };

    template<typename U>
    struct Slot<U, false>
    {
        std::unique_ptr<ygg::buffer::SegmentedBuffer> arena;
        std::unique_ptr<ygg::buffer::Buffer> buffer;
        ygg::buffer::IndexedHashSet<U> container;
        size_t parent_size = 0;

        static size_t hash(const Data<U>& builder) noexcept { return ygg::buffer::IndexedHashSet<U>::hash(builder); }

        Slot() : arena(std::make_unique<ygg::buffer::SegmentedBuffer>()), buffer(std::make_unique<ygg::buffer::Buffer>()), container(*buffer, *arena) {}

        void clear() noexcept
        {
            arena->clear();
            container.clear();
        }

        size_t memory_usage() const noexcept { return (arena ? arena->capacity() : 0) + (buffer ? buffer->buf_.capacity() : 0) + container.memory_usage(); }
    };

    const BasicSymbolRepository* m_parent;
    Slot<T> m_slot;

    void clear_slot() noexcept
    {
        m_slot.clear();
        m_slot.parent_size = m_parent ? m_parent->size() : size_t { 0 };
    }

public:
    /**
     * Local methods access only the current repository layer.
     * Handle-producing methods return raw handles because the caller already knows the context.
     */

    std::optional<Index<T>> find_local_with_hash(const Data<T>& builder, size_t h) const noexcept
    {
        const auto& container = m_slot.container;
        assert(h == container.hash(builder));

        if (auto index_or_nullopt = container.find_with_hash(builder, h))
            return Index<T>(m_slot.parent_size + ygg::uint_t(*index_or_nullopt));

        return std::nullopt;
    }

    std::optional<Index<T>> find_local(const Data<T>& builder) const noexcept { return find_local_with_hash(builder, BasicSymbolRepository::hash(builder)); }

    std::pair<Index<T>, bool> get_or_create_local_with_hash(Data<T>& builder, size_t h)
    {
        auto& container = m_slot.container;

        if (auto index_or_nullopt = container.find_with_hash(builder, h))
            return { Index<T>(m_slot.parent_size + ygg::uint_t(*index_or_nullopt)), false };

        builder.index.value = m_slot.parent_size + container.size();

        const auto index = container.insert_new_with_hash(h, builder);
        return { Index<T>(m_slot.parent_size + ygg::uint_t(index)), true };
    }

    std::pair<Index<T>, bool> get_or_create_local(Data<T>& builder) { return get_or_create_local_with_hash(builder, BasicSymbolRepository::hash(builder)); }

    /// Inserts without probing first. The caller must have established that the symbol is absent from
    /// this layer, e.g. by a preceding find_local_with_hash with the same hash.
    Index<T> insert_new_local_with_hash(Data<T>& builder, size_t h)
    {
        auto& container = m_slot.container;

        builder.index.value = m_slot.parent_size + container.size();

        return Index<T>(m_slot.parent_size + ygg::uint_t(container.insert_new_with_hash(h, builder)));
    }

    const Data<T>& at_local(Index<T> index) const
    {
        const auto parent_size = m_slot.parent_size;
        if (index.value < parent_size || index.value >= size())
            throw std::out_of_range("Symbol index not found in local repository.");

        return m_slot.container[Index<T>(index.value - parent_size)];
    }

    const Data<T>& front_local() const
    {
        if (m_slot.container.empty())
            throw std::out_of_range("Symbol index not found in local repository.");
        return m_slot.container.front();
    }

    size_t local_size() const noexcept { return m_slot.container.size(); }

    size_t size() const noexcept { return m_slot.parent_size + m_slot.container.size(); }

    size_t parent_size() const noexcept { return m_slot.parent_size; }

    bool is_local(Index<T> index) const noexcept { return index != Index<T>::max() && ygg::uint_t(index) >= m_slot.parent_size && ygg::uint_t(index) < size(); }

    bool exists_parent_mutation() const noexcept
    {
        if (!m_parent)
            return false;

        return m_parent->size() > m_slot.parent_size;
    }

    /**
     * Common methods do not depend on lookup scope.
     */

    BasicSymbolRepository(const BasicSymbolRepository* parent = nullptr) : m_parent(parent), m_slot() { clear_slot(); }

    BasicSymbolRepository(const BasicSymbolRepository&) = delete;
    BasicSymbolRepository& operator=(const BasicSymbolRepository&) = delete;
    BasicSymbolRepository(BasicSymbolRepository&&) noexcept = default;
    BasicSymbolRepository& operator=(BasicSymbolRepository&&) noexcept = default;

    void clear() noexcept { clear_slot(); }

    size_t memory_usage() const noexcept { return m_slot.memory_usage(); }

    static size_t hash(const Data<T>& builder) noexcept { return Slot<T>::hash(builder); }
};
}  // namespace ygg::formalism

#endif
