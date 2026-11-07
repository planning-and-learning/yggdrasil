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

#include <urd/elements/prehashed.hpp>
#include <urd/repr/pools/pools.hpp>
#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>
#include <urd/elements/flat.hpp>
#include "yggdrasil/core/config.hpp"
#include "yggdrasil/formalism/binding_data.hpp"
#include "yggdrasil/formalism/declarations.hpp"

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
#include <urd/elements/equal_to.hpp>
#include <urd/elements/hash.hpp>
#include <urd/repr/containers/detail/geometric_segment_layout.hpp>
#include <urd/repr/containers/segment_sizes.hpp>
#include <utility>
#include <vector>

namespace ygg::formalism
{

/// The width of a whole object index, the widest a relation repository stores.
inline constexpr auto default_object_index_width = urd::bit_width { static_cast<std::uint8_t>(std::numeric_limits<ygg::uint_t>::digits) };

/// How a relation repository stores its bindings' objects, per object tag: each index whole (urd::word), or in the
/// object_index_width bits the largest needs (urd::bit).
template<typename ObjectTag>
struct RelationRepositoryTraits
{
    using width = urd::word;
};

template<typename ObjectTag, typename T, bool ThreadSafe = false>
class BasicRelationRepository
{
private:
    static constexpr ygg::uint_t kInvalid = std::numeric_limits<ygg::uint_t>::max();

    /// The objects' indices, one row per binding, in a urd pool: rows of one width (the arity) stored flat.
    using internal_container_type = urd::Store<urd::pool<urd::Store<urd::sequence<urd::index<Object<ObjectTag>>>, urd::frozen>>,
                                               typename RelationRepositoryTraits<ObjectTag>::width,
                                               std::conditional_t<ThreadSafe, urd::concurrent, urd::single_threaded>>;
    using Objects = urd::flat_vector<urd::index<Object<ObjectTag>>>;

    struct Slot
    {
        internal_container_type container;
        size_t parent_size = 0;
        urd::index<T> g;
        size_t arity;

        Slot(urd::index<T> g_, size_t arity_, size_t parent_size_, std::uint8_t object_index_width) :
            container(make_container(object_index_width)),
            parent_size(parent_size_),
            g(g_),
            arity(arity_)
        {
        }
        /// A row of the hierarchy from the handle of one of this layer's, and back: the repository's row arithmetic.
        [[nodiscard]] urd::index<Row> row(typename internal_container_type::handle_type handle) const noexcept
        { return urd::index<Row>(static_cast<ygg::uint_t>(parent_size + handle.value)); }
        [[nodiscard]] typename internal_container_type::handle_type handle(urd::index<Row> row) const noexcept
        { return typename internal_container_type::handle_type(static_cast<ygg::uint_t>(row.value - parent_size)); }

        static internal_container_type make_container(std::uint8_t object_index_width)
        {
            if constexpr (std::constructible_from<internal_container_type, urd::bit_width>)
                return internal_container_type(urd::bit_width { object_index_width });
            else
                return internal_container_type();
        }

        /// A binding of this relation has one object per argument.
        void check_arity(size_t objects) const
        {
            if (objects != arity)
                throw std::invalid_argument("BasicRelationRepository: wrong number of objects for the relation.");
        }
    };

    class SequentialSlotDirectory
    {
    public:
        Slot* find(ygg::uint_t gi) noexcept { return find_in(*this, gi); }
        [[nodiscard]] const Slot* find(ygg::uint_t gi) const noexcept { return find_in(*this, gi); }

        Slot& get_or_create(urd::index<T> g, size_t arity, size_t parent_size, std::uint8_t object_index_width)
        {
            const auto gi = ygg::uint_t(g.value);
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
        void for_each(F function)
        {
            for (auto& slot : m_slots)
                function(slot);
        }

        template<typename F>
        void for_each(F function) const
        {
            for (const auto& slot : m_slots)
                function(slot);
        }

        [[nodiscard]] size_t memory_usage() const noexcept { return m_forward.capacity() * sizeof(ygg::uint_t) + m_slots.capacity() * sizeof(Slot); }

    private:
        template<typename Self>
        static auto* find_in(Self& self, ygg::uint_t gi) noexcept
        {
            using Result = decltype(&self.m_slots[0]);
            if (gi >= self.m_forward.size() || self.m_forward[gi] == kInvalid)
                return Result { nullptr };
            return &self.m_slots[self.m_forward[gi]];
        }

        urd::Store<urd::sequence<ygg::uint_t>> m_forward;
        urd::Store<urd::sequence<Slot>> m_slots;
    };

    class ConcurrentSlotDirectory
    {
    private:
        using Layout = urd::repr::detail::GeometricSegmentLayout<urd::repr::default_first_segment_size, ygg::uint_t>;
        static constexpr auto kMaxIndex = std::numeric_limits<ygg::uint_t>::max() - 1;
        static constexpr size_t kNumSegments = Layout::segment_index(kMaxIndex) + 1;

        struct Segment
        {
            explicit Segment(size_t size_) : entries(std::make_unique<std::atomic<Slot*>[]>(size_)), size(size_)
            {
                for (size_t i = 0; i < size_; ++i)
                    entries[i].store(nullptr, std::memory_order_relaxed);
            }

            std::unique_ptr<std::atomic<Slot*>[]> entries;
            size_t size;
        };

        // The slots are reached through atomic pointers, so const members find them mutable too.
        [[nodiscard]] Slot* lookup(ygg::uint_t gi) const noexcept
        {
            const auto [segment_index, offset] = locate(gi);
            const auto* segment = m_segments[segment_index].load(std::memory_order_acquire);
            return segment ? segment->entries[offset].load(std::memory_order_acquire) : nullptr;
        }

        template<typename F>
        void visit_slots(F function) const
        {
            for (const auto& segment_ptr : m_segments)
            {
                const auto* segment = segment_ptr.load(std::memory_order_relaxed);
                if (!segment)
                    continue;
                for (size_t i = 0; i < segment->size; ++i)
                    if (auto* slot = segment->entries[i].load(std::memory_order_relaxed))
                        function(*slot);
            }
        }

        static std::pair<size_t, size_t> locate(ygg::uint_t gi) noexcept
        {
            const auto segment = Layout::segment_index(gi);
            return { segment, Layout::segment_offset(gi, segment) };
        }

        static constexpr size_t segment_size(size_t segment) noexcept
        { return segment + 1 == kNumSegments ? Layout::segment_offset(kMaxIndex, segment) + 1 : Layout::segment_capacity(segment); }

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
                // Each segment and slot was released from a unique_ptr when published.
                const auto segment = std::unique_ptr<Segment>(segment_ptr.load(std::memory_order_relaxed));
                if (!segment)
                    continue;
                for (size_t i = 0; i < segment->size; ++i)
                    std::unique_ptr<Slot>(segment->entries[i].load(std::memory_order_relaxed)).reset();
            }
        }

        ConcurrentSlotDirectory(const ConcurrentSlotDirectory&) = delete;
        ConcurrentSlotDirectory& operator=(const ConcurrentSlotDirectory&) = delete;
        ConcurrentSlotDirectory(ConcurrentSlotDirectory&&) = delete;
        ConcurrentSlotDirectory& operator=(ConcurrentSlotDirectory&&) = delete;

        Slot* find(ygg::uint_t gi) noexcept { return lookup(gi); }
        [[nodiscard]] const Slot* find(ygg::uint_t gi) const noexcept { return lookup(gi); }

        Slot& get_or_create(urd::index<T> g, size_t arity, size_t parent_size, std::uint8_t object_index_width)
        {
            const auto [segment_index, offset] = locate(ygg::uint_t(g.value));
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
        void for_each(F function)
        {
            visit_slots([&](Slot& slot) { function(slot); });
        }

        template<typename F>
        void for_each(F function) const
        {
            visit_slots([&](Slot& slot) { function(std::as_const(slot)); });
        }

        [[nodiscard]] size_t memory_usage() const noexcept
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

    Slot& get_or_create_slot(urd::index<T> g, size_t arity)
    {
        if (g == urd::index<T>())
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

    [[nodiscard]] const Slot* find_slot(urd::index<T> g) const noexcept { return g == urd::index<T>() ? nullptr : m_slots.find(ygg::uint_t(g.value)); }

    Slot* find_slot(urd::index<T> g) noexcept { return g == urd::index<T>() ? nullptr : m_slots.find(ygg::uint_t(g.value)); }

public:
    static constexpr bool thread_safe = ThreadSafe;

    using container_type = internal_container_type;

private:
    using ConstViewType = decltype(std::declval<const container_type&>().view(typename container_type::handle_type()));

public:
    /**
     * Local methods access only the current repository layer.
     * Handle-producing methods return raw handles because the caller already knows the context.
     */

    [[nodiscard]] std::optional<urd::index<Row>> find_local_with_hash(const urd::data<RelationBinding<T, ObjectTag>>& builder, size_t h) const
    {
        const auto g = builder.relation;

        const auto* slot = find_slot(g);
        if (!slot)
            return std::nullopt;
        slot->check_arity(builder.objects.size());

        if (const auto found = slot->container.find(urd::prehashed<Objects> { builder.objects, h }); !found.is_none())
            return slot->row(found);

        return std::nullopt;
    }

    [[nodiscard]] std::optional<urd::index<Row>> find_local(const urd::data<RelationBinding<T, ObjectTag>>& builder) const
    { return find_local_with_hash(builder, BasicRelationRepository::hash(builder)); }

    std::pair<urd::index<Row>, bool> insert_local_with_hash(const urd::data<RelationBinding<T, ObjectTag>>& builder, size_t h)
    {
        if (const auto index = find_local_with_hash(builder, h))
            return { *index, false };

        return create_local_with_hash(builder, h);
    }

    std::pair<urd::index<Row>, bool> insert_local(const urd::data<RelationBinding<T, ObjectTag>>& builder)
    { return insert_local_with_hash(builder, BasicRelationRepository::hash(builder)); }

    /// Completes a hierarchy-wide miss by rechecking this lane before publishing storage.
    std::pair<urd::index<Row>, bool> create_local_with_hash(const urd::data<RelationBinding<T, ObjectTag>>& builder, size_t h)
    {
        auto& slot = get_or_create_slot(builder.relation, builder.objects.size());
        slot.check_arity(builder.objects.size());
        const auto [handle, created] = slot.container.insert(urd::prehashed<Objects> { builder.objects, h });
        return { slot.row(handle), created };
    }

    [[nodiscard]] ConstViewType at_local(ygg::formalism::RelationBindingIndex<T, ObjectTag> index) const
    {
        const auto& [g, row] = index;

        const auto* slot = find_slot(g);
        if (!slot || row.value < slot->parent_size || row.value >= slot->parent_size + slot->container.size())
            throw std::out_of_range("Relation binding index not found in local repository.");

        return slot->container.view(slot->handle(row));
    }

    [[nodiscard]] ConstViewType front_local(urd::index<T> g) const
    {
        const auto* slot = find_slot(g);
        if (!slot || slot->container.size() == 0)
            throw std::out_of_range("Relation binding index not found in local repository.");
        return slot->container.view(typename container_type::handle_type(0));
    }

    [[nodiscard]] size_t local_size(urd::index<T> g) const noexcept
    {
        const auto* slot = find_slot(g);
        return slot ? slot->container.size() : 0;
    }

    [[nodiscard]] size_t size(urd::index<T> g) const noexcept
    {
        const auto* slot = find_slot(g);
        if (!slot)
            return m_parent ? m_parent->size(g) : 0;

        return slot->parent_size + slot->container.size();
    }

    [[nodiscard]] size_t parent_size(urd::index<T> g) const noexcept
    {
        const auto* slot = find_slot(g);
        return slot ? slot->parent_size : (m_parent ? m_parent->size(g) : 0);
    }

    [[nodiscard]] bool is_local(ygg::formalism::RelationBindingIndex<T, ObjectTag> index) const noexcept
    {
        const auto& [g, row] = index;
        if (g == urd::index<T>() || row == urd::index<Row>())
            return false;

        const auto* slot = find_slot(g);
        if (!slot)
            return false;

        return ygg::uint_t(row.value) >= slot->parent_size && ygg::uint_t(row.value) < slot->parent_size + slot->container.size();
    }

    [[nodiscard]] bool exists_parent_mutation(urd::index<T> g) const noexcept
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

    explicit BasicRelationRepository(const BasicRelationRepository* parent = nullptr, urd::bit_width object_index_width = default_object_index_width) :
        m_parent(parent),
        m_object_index_width(object_index_width.bits),
        m_slots()
    {
        if (m_object_index_width == 0 || m_object_index_width > default_object_index_width.bits)
            throw std::invalid_argument("BasicRelationRepository: object index width must be between 1 and the encoded object width.");
        if (m_parent && m_object_index_width < m_parent->m_object_index_width)
            throw std::invalid_argument("BasicRelationRepository: child object index width must not be smaller than its parent's.");
        clear_slots();
    }
    ~BasicRelationRepository() = default;
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
    [[nodiscard]] size_t memory_usage() const noexcept
    {
        size_t bytes = m_slots.memory_usage();
        m_slots.for_each([&](const Slot& slot) { bytes += urd::memory_usage(slot.container); });
        return bytes;
    }

    [[nodiscard]] std::uint8_t get_object_index_width() const noexcept { return m_object_index_width; }

    static size_t hash(const urd::data<RelationBinding<T, ObjectTag>>& builder) noexcept { return urd::hash_range(builder.objects); }
};
}  // namespace ygg::formalism

#endif
