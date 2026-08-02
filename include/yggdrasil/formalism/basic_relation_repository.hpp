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
#include <yggdrasil/containers/bit_packed_array_set.hpp>
#include <yggdrasil/containers/block_array_set.hpp>
#include <yggdrasil/containers/detail/geometric_segment_layout.hpp>
#include <yggdrasil/containers/tuple.hpp>
#include <yggdrasil/core/types.hpp>
#include <yggdrasil/formalism/binding_data.hpp>
#include <yggdrasil/formalism/binding_index.hpp>
#include <yggdrasil/formalism/declarations.hpp>
#include <yggdrasil/formalism/object_index.hpp>
#include <yggdrasil/semantics/equal_to.hpp>
#include <yggdrasil/semantics/hash.hpp>

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
    template<std::unsigned_integral Block, bit::BlockCoder<Block> Coder, bool ThreadSafe>
    using container_type = ygg::BlockArraySet<Block, Coder, 16, ThreadSafe>;

    template<std::unsigned_integral Block, bit::BlockCoder<Block> Coder, bool ThreadSafe>
    static container_type<Block, Coder, ThreadSafe> make(size_t arity, std::uint8_t)
    {
        return container_type<Block, Coder, ThreadSafe>(arity);
    }
};

struct BitPackedArraySetStorage
{
    template<std::unsigned_integral Block, bit::BlockCoder<Block> Coder, bool ThreadSafe>
    using container_type = ygg::BitPackedArraySet<Block, Coder, 16, ThreadSafe>;

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
    template<std::unsigned_integral Block>
    struct Coder
    {
        using value_type = Index<Object<ObjectTag>>;

        static constexpr value_type decode(Block block) noexcept { return value_type(block); }
        static constexpr Block encode(value_type value) noexcept { return static_cast<Block>((static_cast<ygg::uint_t>(value))); }
    };

    static constexpr ygg::uint_t kInvalid = std::numeric_limits<ygg::uint_t>::max();

    using storage_type = typename RelationRepositoryTraits<ObjectTag>::storage_type;

    static auto make_container(size_t arity, std::uint8_t object_index_width)
    {
        return storage_type::template make<ygg::uint_t, Coder<ygg::uint_t>, ThreadSafe>(arity, object_index_width);
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

    class SequentialSlotDirectory
    {
    public:
        Slot* find(ygg::uint_t gi) noexcept
        {
            if (gi >= m_forward.size() || m_forward[gi] == kInvalid)
                return nullptr;
            return &m_slots[m_forward[gi]];
        }

        const Slot* find(ygg::uint_t gi) const noexcept { return const_cast<SequentialSlotDirectory*>(this)->find(gi); }

        Slot& get_or_create(Index<T> g, size_t arity, size_t parent_size, std::uint8_t object_index_width)
        {
            const auto gi = ygg::uint_t(g);
            if (gi >= m_forward.size())
                m_forward.resize(gi + 1, kInvalid);

            if (m_forward[gi] == kInvalid)
            {
                const auto slot = static_cast<ygg::uint_t>(m_slots.size());
                m_slots.emplace_back(g, arity, parent_size, object_index_width);
                m_forward[gi] = slot;
            }

            return m_slots[m_forward[gi]];
        }

        template<typename F>
        void for_each(F&& function)
        {
            for (auto& slot : m_slots)
                function(slot);
        }

        template<typename F>
        void for_each(F&& function) const
        {
            for (const auto& slot : m_slots)
                function(slot);
        }

        size_t memory_usage() const noexcept { return m_forward.capacity() * sizeof(ygg::uint_t) + m_slots.capacity() * sizeof(Slot); }

    private:
        std::vector<ygg::uint_t> m_forward;
        std::vector<Slot> m_slots;
    };

    class ConcurrentSlotDirectory
    {
    private:
        using Layout = ::ygg::detail::GeometricSegmentLayout<32, ygg::uint_t>;
        static constexpr auto kMaxIndex = std::numeric_limits<ygg::uint_t>::max() - 1;
        static constexpr size_t kNumSegments = Layout::segment_index(kMaxIndex) + 1;

        struct Segment
        {
            explicit Segment(size_t size) : entries(std::make_unique<std::atomic<Slot*>[]>(size)), size(size)
            {
                for (size_t i = 0; i < size; ++i)
                    entries[i].store(nullptr, std::memory_order_relaxed);
            }

            std::unique_ptr<std::atomic<Slot*>[]> entries;
            size_t size;
        };

        static std::pair<size_t, size_t> locate(ygg::uint_t gi) noexcept
        {
            const auto segment = Layout::segment_index(gi);
            return { segment, Layout::segment_offset(gi, segment) };
        }

        static constexpr size_t segment_size(size_t segment) noexcept
        {
            return segment + 1 == kNumSegments ? Layout::segment_offset(kMaxIndex, segment) + 1 : Layout::segment_capacity(segment);
        }

        Segment& get_or_create_segment(size_t index)
        {
            auto* segment = m_segments[index].load(std::memory_order_acquire);
            if (segment)
                return *segment;

            auto candidate = std::make_unique<Segment>(segment_size(index));
            auto* expected = static_cast<Segment*>(nullptr);
            if (m_segments[index].compare_exchange_strong(expected, candidate.get(), std::memory_order_release, std::memory_order_acquire))
                segment = candidate.release();
            else
                segment = expected;

            return *segment;
        }

    public:
        ConcurrentSlotDirectory() noexcept
        {
            for (auto& segment : m_segments)
                segment.store(nullptr, std::memory_order_relaxed);
        }

        ~ConcurrentSlotDirectory()
        {
            for (auto& segment_ptr : m_segments)
            {
                auto* segment = segment_ptr.load(std::memory_order_relaxed);
                if (!segment)
                    continue;
                for (size_t i = 0; i < segment->size; ++i)
                    delete segment->entries[i].load(std::memory_order_relaxed);
                delete segment;
            }
        }

        ConcurrentSlotDirectory(const ConcurrentSlotDirectory&) = delete;
        ConcurrentSlotDirectory& operator=(const ConcurrentSlotDirectory&) = delete;
        ConcurrentSlotDirectory(ConcurrentSlotDirectory&&) = delete;
        ConcurrentSlotDirectory& operator=(ConcurrentSlotDirectory&&) = delete;

        Slot* find(ygg::uint_t gi) noexcept
        {
            const auto [segment_index, offset] = locate(gi);
            const auto* segment = m_segments[segment_index].load(std::memory_order_acquire);
            return segment ? segment->entries[offset].load(std::memory_order_acquire) : nullptr;
        }

        const Slot* find(ygg::uint_t gi) const noexcept { return const_cast<ConcurrentSlotDirectory*>(this)->find(gi); }

        Slot& get_or_create(Index<T> g, size_t arity, size_t parent_size, std::uint8_t object_index_width)
        {
            const auto [segment_index, offset] = locate(ygg::uint_t(g));
            auto& entry = get_or_create_segment(segment_index).entries[offset];

            auto* slot = entry.load(std::memory_order_acquire);
            if (slot)
                return *slot;

            auto candidate = std::make_unique<Slot>(g, arity, parent_size, object_index_width);
            auto* expected = static_cast<Slot*>(nullptr);
            if (entry.compare_exchange_strong(expected, candidate.get(), std::memory_order_release, std::memory_order_acquire))
                slot = candidate.release();
            else
                slot = expected;

            return *slot;
        }

        template<typename F>
        void for_each(F&& function)
        {
            for (auto& segment_ptr : m_segments)
            {
                auto* segment = segment_ptr.load(std::memory_order_relaxed);
                if (!segment)
                    continue;
                for (size_t i = 0; i < segment->size; ++i)
                    if (auto* slot = segment->entries[i].load(std::memory_order_relaxed))
                        function(*slot);
            }
        }

        template<typename F>
        void for_each(F&& function) const
        {
            const_cast<ConcurrentSlotDirectory*>(this)->for_each([&](Slot& slot) { function(std::as_const(slot)); });
        }

        size_t memory_usage() const noexcept
        {
            size_t bytes = 0;
            for_each([&](const Slot&) { bytes += sizeof(Slot); });
            for (const auto& segment_ptr : m_segments)
                if (const auto* segment = segment_ptr.load(std::memory_order_relaxed))
                    bytes += sizeof(Segment) + segment->size * sizeof(std::atomic<Slot*>);
            return bytes;
        }

    private:
        std::array<std::atomic<Segment*>, kNumSegments> m_segments;
    };

    using SlotDirectory = std::conditional_t<ThreadSafe, ConcurrentSlotDirectory, SequentialSlotDirectory>;

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
        return m_slots.get_or_create(g, arity, parent_size, m_object_index_width);
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

    using container_type = internal_container_type;
    using ConstViewType = typename container_type::ConstArrayView;

    /**
     * Local methods access only the current repository layer.
     * Handle-producing methods return raw handles because the caller already knows the context.
     */

    std::optional<Index<Row>> find_local_with_hash(const Data<RelationBinding<T, ObjectTag>>& builder, size_t h) const noexcept
    {
        const auto g = builder.relation;

        const auto* slot = find_slot(g);
        if (!slot)
            return std::nullopt;

        if (auto row_or_nullopt = slot->container.find_with_hash(builder.objects, h))
            return Index<Row>(slot->parent_size + *row_or_nullopt);

        return std::nullopt;
    }

    std::optional<Index<Row>> find_local(const Data<RelationBinding<T, ObjectTag>>& builder) const noexcept
    {
        return find_local_with_hash(builder, BasicRelationRepository::hash(builder));
    }

    std::pair<Index<Row>, bool> get_or_create_local_with_hash(const Data<RelationBinding<T, ObjectTag>>& builder, size_t h)
    {
        if (const auto index = find_local_with_hash(builder, h))
            return { *index, false };

        return create_local_with_hash(builder, h);
    }

    std::pair<Index<Row>, bool> get_or_create_local(const Data<RelationBinding<T, ObjectTag>>& builder)
    {
        return get_or_create_local_with_hash(builder, BasicRelationRepository::hash(builder));
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
