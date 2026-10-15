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

#ifndef YGG_FORMALISM_BASIC_RELATION_REPOSITORY_HPP_
#define YGG_FORMALISM_BASIC_RELATION_REPOSITORY_HPP_

#include "yggdrasil/containers/bit_packed_array_set.hpp"
#include "yggdrasil/containers/block_array_set.hpp"
#include "yggdrasil/containers/detail/slot_directory.hpp"
#include "yggdrasil/containers/tuple.hpp"
#include "yggdrasil/core/types.hpp"
#include "yggdrasil/formalism/binding_data.hpp"
#include "yggdrasil/formalism/binding_index.hpp"
#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/ids/index_coder.hpp"
#include "yggdrasil/semantics/equal_to.hpp"
#include "yggdrasil/semantics/hash.hpp"

#include <array>
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace ygg::formalism
{

struct RelationRepositoryConfig
{
    static constexpr std::uint8_t default_object_index_width = static_cast<std::uint8_t>(std::numeric_limits<ygg::uint_t>::digits);

    explicit RelationRepositoryConfig(std::uint8_t object_index_width = default_object_index_width) : object_index_width(object_index_width)
    {
        if (object_index_width == 0 || object_index_width > default_object_index_width)
            throw std::invalid_argument("RelationRepositoryConfig: object index width must be between 1 and the encoded object width.");
    }

    std::uint8_t object_index_width;
};

struct BlockArraySetStorage
{
private:
    template<std::unsigned_integral Block, bit::BlockCoder<Block> Coder, bool ThreadSafe>
    using container_type = ygg::BlockArraySet<Block, Coder, 16, ThreadSafe>;

public:
    template<std::unsigned_integral Block, bit::BlockCoder<Block> Coder, bool ThreadSafe>
    static container_type<Block, Coder, ThreadSafe> make(size_t arity, std::uint8_t)
    {
        return container_type<Block, Coder, ThreadSafe>(arity);
    }
};

struct BitPackedArraySetStorage
{
private:
    template<std::unsigned_integral Block, bit::BlockCoder<Block> Coder, bool ThreadSafe>
    using container_type = ygg::BitPackedArraySet<Block, Coder, 16, ThreadSafe>;

public:
    template<std::unsigned_integral Block, bit::BlockCoder<Block> Coder, bool ThreadSafe>
    static container_type<Block, Coder, ThreadSafe> make(size_t arity, std::uint8_t object_index_width)
    {
        return container_type<Block, Coder, ThreadSafe>(arity, object_index_width);
    }
};

template<typename ObjectTag>
struct RelationRepositoryTraits
{
    using storage_type = BlockArraySetStorage;
};

template<typename ObjectTag, typename T, bool ThreadSafe = false>
class BasicRelationRepository
{
private:
    using storage_type = typename RelationRepositoryTraits<ObjectTag>::storage_type;

    static auto make_container(size_t arity, std::uint8_t object_index_width)
    {
        return storage_type::template make<ygg::uint_t, IndexCoder<Index<Object<ObjectTag>>, ygg::uint_t>, ThreadSafe>(arity, object_index_width);
    }

    using internal_container_type = decltype(make_container(size_t {}, std::uint8_t {}));

    struct Slot
    {
        Index<T> g;
        internal_container_type container;
        size_t parent_size = 0;

        Slot(Index<T> g, size_t arity, size_t parent_size, std::uint8_t object_index_width) :
            g(g),
            container(make_container(arity, object_index_width)),
            parent_size(parent_size)
        {
        }
    };

    using SlotDirectory = ::ygg::detail::SlotDirectory<Slot, ThreadSafe>;

    const BasicRelationRepository* m_parent;
    std::uint8_t m_object_index_width;
    SlotDirectory m_slots;

    Slot& get_or_create_slot(Index<T> g, size_t arity)
    {
        if (g == Index<T>::max())
            throw std::invalid_argument("BasicRelationRepository: the maximum relation index is reserved.");

        if (auto* slot = find_slot(g))
            return *slot;

        const auto parent_size = m_parent ? m_parent->size(g) : size_t { 0 };
        return m_slots.get_or_create(ygg::uint_t(g), g, arity, parent_size, m_object_index_width);
    }

    void clear_slots() noexcept
    {
        m_slots.for_each(
            [&](Slot& slot)
            {
                slot.container.clear();
                slot.parent_size = m_parent ? m_parent->size(slot.g) : size_t { 0 };
            });
    }

    const Slot* find_slot(Index<T> g) const noexcept { return g == Index<T>::max() ? nullptr : m_slots.find(ygg::uint_t(g)); }

    Slot* find_slot(Index<T> g) noexcept { return g == Index<T>::max() ? nullptr : m_slots.find(ygg::uint_t(g)); }

public:
    static constexpr bool thread_safe = ThreadSafe;
    static constexpr bool stores_bindings = true;

    using container_type = internal_container_type;

private:
    using ConstViewType = typename container_type::ConstArrayView;

public:
    /**
     * Local methods access only the current repository layer.
     * Handle-producing methods return raw handles because the caller already knows the context.
     */

    std::optional<Index<Row>> find_local_with_hash(const Data<RelationBinding<T, ObjectTag>>& builder, size_t h) const
    {
        const auto g = builder.relation;

        const auto* slot = find_slot(g);
        if (!slot)
            return std::nullopt;

        if (auto row_or_nullopt = slot->container.find_with_hash(builder.objects, h))
            return Index<Row>(slot->parent_size + *row_or_nullopt);

        return std::nullopt;
    }

    /// Performs a local lookup without locking the underlying hash shard. The
    /// underlying collection's unsafe publication, lifetime, and immutability
    /// preconditions apply.
    std::optional<Index<Row>> find_local_unsafe_with_hash(const Data<RelationBinding<T, ObjectTag>>& builder, size_t h) const
    {
        const auto g = builder.relation;

        const auto* slot = find_slot(g);
        if (!slot)
            return std::nullopt;

        if (auto row_or_nullopt = slot->container.find_unsafe_with_hash(builder.objects, h))
            return Index<Row>(slot->parent_size + *row_or_nullopt);

        return std::nullopt;
    }

    std::optional<Index<Row>> find_local(const Data<RelationBinding<T, ObjectTag>>& builder) const
    {
        return find_local_with_hash(builder, BasicRelationRepository::hash(builder));
    }

    std::pair<Index<Row>, bool> insert_local_with_hash(const Data<RelationBinding<T, ObjectTag>>& builder, size_t h)
    {
        if (const auto index = find_local_with_hash(builder, h))
            return { *index, false };

        return create_local_with_hash(builder, h);
    }

    std::pair<Index<Row>, bool> insert_local(const Data<RelationBinding<T, ObjectTag>>& builder)
    {
        return insert_local_with_hash(builder, BasicRelationRepository::hash(builder));
    }

    /// Completes a hierarchy-wide miss by rechecking this lane before publishing storage.
    std::pair<Index<Row>, bool> create_local_with_hash(const Data<RelationBinding<T, ObjectTag>>& builder, size_t h)
    {
        auto& slot = get_or_create_slot(builder.relation, builder.objects.size());
        const auto [row, created] = slot.container.complete_miss_with_hash(h, builder.objects);
        return { Index<Row>(slot.parent_size + row), created };
    }

    ConstViewType at_local(Index<RelationBinding<T, ObjectTag>> index) const
    {
        const auto& [g, row] = index;

        const auto* slot = find_slot(g);
        if (!slot || row.value < slot->parent_size || row.value >= slot->parent_size + slot->container.size())
            throw std::out_of_range("Relation binding index not found in local repository.");

        return slot->container[row.value - slot->parent_size];
    }

    ConstViewType front_local(Index<T> g) const
    {
        const auto* slot = find_slot(g);
        if (!slot || slot->container.empty())
            throw std::out_of_range("Relation binding index not found in local repository.");
        return slot->container.front();
    }

    size_t local_size(Index<T> g) const noexcept
    {
        const auto* slot = find_slot(g);
        return slot ? slot->container.size() : 0;
    }

    size_t size(Index<T> g) const noexcept
    {
        const auto* slot = find_slot(g);
        if (!slot)
            return m_parent ? m_parent->size(g) : 0;

        return slot->parent_size + slot->container.size();
    }

    size_t parent_size(Index<T> g) const noexcept
    {
        const auto* slot = find_slot(g);
        return slot ? slot->parent_size : (m_parent ? m_parent->size(g) : 0);
    }

    bool is_local(Index<RelationBinding<T, ObjectTag>> index) const noexcept
    {
        const auto& [g, row] = index;
        if (g == Index<T>::max() || row == Index<Row>::max())
            return false;

        const auto* slot = find_slot(g);
        if (!slot)
            return false;

        return ygg::uint_t(row) >= slot->parent_size && ygg::uint_t(row) < slot->parent_size + slot->container.size();
    }

    bool exists_parent_mutation(Index<T> g) const noexcept
    {
        if (!m_parent)
            return false;

        const auto* slot = find_slot(g);
        if (!slot)
            return false;

        return m_parent->size(g) > slot->parent_size;
    }

    /**
     * Common methods do not depend on lookup scope.
     */

    BasicRelationRepository(const BasicRelationRepository* parent = nullptr) : BasicRelationRepository(parent, RelationRepositoryConfig()) {}

    BasicRelationRepository(const BasicRelationRepository* parent, RelationRepositoryConfig config) :
        m_parent(parent),
        m_object_index_width(config.object_index_width),
        m_slots()
    {
        if (m_parent && m_object_index_width < m_parent->m_object_index_width)
            throw std::invalid_argument("BasicRelationRepository: child object index width must not be smaller than its parent's.");
        clear_slots();
    }
    BasicRelationRepository(const BasicRelationRepository& other) = delete;
    BasicRelationRepository& operator=(const BasicRelationRepository& other) = delete;
    BasicRelationRepository(BasicRelationRepository&&) noexcept
        requires(!ThreadSafe)
    = default;
    BasicRelationRepository& operator=(BasicRelationRepository&&) noexcept
        requires(!ThreadSafe)
    = default;
    BasicRelationRepository(BasicRelationRepository&&)
        requires ThreadSafe
    = delete;
    BasicRelationRepository& operator=(BasicRelationRepository&&)
        requires ThreadSafe
    = delete;

    /// @brief Clear the repository but keep memory allocated.
    void clear() noexcept { clear_slots(); }

    /// @brief Retained dynamic storage owned by this repository layer.
    size_t memory_usage() const noexcept
    {
        size_t bytes = m_slots.memory_usage();
        m_slots.for_each([&](const Slot& slot) { bytes += slot.container.memory_usage(); });
        return bytes;
    }

    std::uint8_t get_object_index_width() const noexcept { return m_object_index_width; }

    static size_t hash(const Data<RelationBinding<T, ObjectTag>>& builder) noexcept { return container_type::hash(builder.objects); }
};
}  // namespace ygg::formalism

#endif
