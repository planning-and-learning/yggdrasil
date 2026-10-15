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

#ifndef YGG_CONTAINERS_DETAIL_SLOT_DIRECTORY_HPP_
#define YGG_CONTAINERS_DETAIL_SLOT_DIRECTORY_HPP_

#include "yggdrasil/containers/detail/geometric_segment_layout.hpp"
#include "yggdrasil/core/types.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace ygg::detail
{

/// Maps indices to lazily created slots.
template<typename Slot>
class SequentialSlotDirectory
{
private:
    static constexpr ygg::uint_t kInvalid = std::numeric_limits<ygg::uint_t>::max();

public:
    Slot* find(ygg::uint_t gi) noexcept
    {
        if (gi >= m_forward.size() || m_forward[gi] == kInvalid)
            return nullptr;
        return &m_slots[m_forward[gi]];
    }

    const Slot* find(ygg::uint_t gi) const noexcept { return const_cast<SequentialSlotDirectory*>(this)->find(gi); }

    template<typename... Args>
    Slot& get_or_create(ygg::uint_t gi, Args&&... args)
    {
        if (gi >= m_forward.size())
            m_forward.resize(gi + 1, kInvalid);

        if (m_forward[gi] == kInvalid)
        {
            const auto slot = static_cast<ygg::uint_t>(m_slots.size());
            m_slots.emplace_back(std::forward<Args>(args)...);
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

/// Lock-free variant: segments and slots are published with CAS, so concurrent creation and lookup may overlap.
template<typename Slot>
class ConcurrentSlotDirectory
{
private:
    using Layout = GeometricSegmentLayout<32, ygg::uint_t>;
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

    template<typename... Args>
    Slot& get_or_create(ygg::uint_t gi, Args&&... args)
    {
        const auto [segment_index, offset] = locate(gi);
        auto& entry = get_or_create_segment(segment_index).entries[offset];

        auto* slot = entry.load(std::memory_order_acquire);
        if (slot)
            return *slot;

        auto candidate = std::make_unique<Slot>(std::forward<Args>(args)...);
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

template<typename Slot, bool ThreadSafe>
using SlotDirectory = std::conditional_t<ThreadSafe, ConcurrentSlotDirectory<Slot>, SequentialSlotDirectory<Slot>>;

}  // namespace ygg::detail

#endif
