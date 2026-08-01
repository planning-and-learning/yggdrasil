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
#include <yggdrasil/containers/detail/threading.hpp>
#include <yggdrasil/containers/indexed_hash_set.hpp>
#include <yggdrasil/containers/tuple.hpp>
#include <yggdrasil/core/types.hpp>
#include <yggdrasil/formalism/declarations.hpp>
#include <yggdrasil/semantics/equal_to.hpp>
#include <yggdrasil/semantics/hash.hpp>

namespace ygg::formalism
{

template<typename T, bool ThreadSafe = false>
class BasicSymbolRepository
{
private:
    template<typename U, bool Trivial = uses_trivial_storage_v<U>>
    struct Slot;

    template<typename U>
    struct Slot<U, true>
    {
        ::ygg::IndexedHashSet<U, ::ygg::Hash<Data<U>>, ::ygg::EqualTo<Data<U>>, 32, ThreadSafe> container;
        size_t parent_size = 0;

        static size_t hash(const Data<U>& builder) noexcept { return decltype(container)::hash(builder); }

        void clear() noexcept { container.clear(); }
        size_t memory_usage() const noexcept { return container.memory_usage(); }
    };

    template<typename U>
    struct Slot<U, false>
    {
        std::unique_ptr<ygg::buffer::SegmentedBuffer> arena;
        std::unique_ptr<ygg::buffer::Buffer> buffer;
        ygg::buffer::IndexedHashSet<U, ::ygg::Hash<Data<U>>, ::ygg::EqualTo<Data<U>>, 32, ThreadSafe> container;
        size_t parent_size = 0;

        static size_t hash(const Data<U>& builder) noexcept { return decltype(container)::hash(builder); }

        Slot() : arena(std::make_unique<ygg::buffer::SegmentedBuffer>()), buffer(std::make_unique<ygg::buffer::Buffer>()), container(*buffer, *arena) {}

        void clear() noexcept
        {
            container.clear();
            arena->clear();
        }

        size_t memory_usage() const noexcept { return (arena ? arena->capacity() : 0) + (buffer ? buffer->buf_.capacity() : 0) + container.memory_usage(); }
    };

    const BasicSymbolRepository* m_parent;
    Slot<T> m_slot;
    [[no_unique_address]] ::ygg::detail::Mutex<ThreadSafe> m_write_mutex;

    void clear_slot() noexcept
    {
        m_slot.clear();
        m_slot.parent_size = m_parent ? m_parent->size() : size_t { 0 };
    }

public:
    static constexpr bool thread_safe = ThreadSafe;

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
        if (const auto index = find_local_with_hash(builder, h))
            return { *index, false };

        return create_local_with_hash(builder, h);
    }

    std::pair<Index<T>, bool> get_or_create_local(Data<T>& builder) { return get_or_create_local_with_hash(builder, BasicSymbolRepository::hash(builder)); }

    /// Completes a hierarchy-wide miss by rechecking this layer before publishing storage.
    std::pair<Index<T>, bool> create_local_with_hash(Data<T>& builder, size_t h)
    {
        return ::ygg::detail::with_lock<ThreadSafe>(m_write_mutex,
                                                    [&]() -> std::pair<Index<T>, bool>
                                                    {
                                                        auto& container = m_slot.container;
                                                        builder.index.value = m_slot.parent_size + container.size();

                                                        const auto [index, created] = container.complete_miss_with_hash(h, builder);
                                                        builder.index.value = m_slot.parent_size + ygg::uint_t(index);
                                                        return { builder.index, created };
                                                    });
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
    BasicSymbolRepository(BasicSymbolRepository&&) noexcept
        requires(!ThreadSafe)
    = default;
    BasicSymbolRepository& operator=(BasicSymbolRepository&&) noexcept
        requires(!ThreadSafe)
    = default;
    BasicSymbolRepository(BasicSymbolRepository&&)
        requires ThreadSafe
    = delete;
    BasicSymbolRepository& operator=(BasicSymbolRepository&&)
        requires ThreadSafe
    = delete;

    void clear() noexcept { clear_slot(); }

    size_t memory_usage() const noexcept { return m_slot.memory_usage(); }

    static size_t hash(const Data<T>& builder) noexcept { return Slot<T>::hash(builder); }
};
}  // namespace ygg::formalism

#endif
