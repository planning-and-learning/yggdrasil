/*
 * Copyright (C) 2025 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_CONTAINERS_RAW_ARRAY_POOL_HPP_
#define YGG_CONTAINERS_RAW_ARRAY_POOL_HPP_

#include "yggdrasil/containers/detail/threading.hpp"
#include "yggdrasil/containers/segmented_vector.hpp"
#include "yggdrasil/core/bit.hpp"
#include "yggdrasil/core/concepts.hpp"
#include "yggdrasil/core/config.hpp"

#include <bit>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace ygg
{

/// ThreadSafe permits concurrent insertion, size queries, and reads after
/// publication was observed through size() or external synchronization.
/// Clear, memory inspection, copy, move, and destruction require quiescence.
template<TriviallyCopyable T, size_t ArraysPerSegment = 1024, bool ThreadSafe = false>
class RawArrayPool
{
    static_assert(bit::is_power_of_two(ArraysPerSegment));

    static constexpr size_t seg_shift = std::countr_zero(ArraysPerSegment);
    static constexpr size_t seg_mask = ArraysPerSegment - 1;

public:
    using value_type = T;
    using ConstView = std::span<const T>;
    static constexpr bool thread_safe = ThreadSafe;

private:
    struct Segment
    {
        std::unique_ptr<T[]> storage;
        size_t capacity;
        size_t size;

        explicit Segment(size_t capacity_) : storage(std::make_unique_for_overwrite<T[]>(capacity_)), capacity(capacity_), size(0) {}

        Segment(const Segment& other) : storage(std::make_unique_for_overwrite<T[]>(other.capacity)), capacity(other.capacity), size(other.size)
        {
            if (size > 0)
                std::memcpy(storage.get(), other.storage.get(), size * sizeof(T));
        }

        Segment& operator=(const Segment& other)
        {
            if (this == &other)
                return *this;

            auto replacement = Segment(other);
            *this = std::move(replacement);
            return *this;
        }

        Segment(Segment&&) noexcept = default;
        Segment& operator=(Segment&&) noexcept = default;

        size_t remaining() const noexcept { return capacity - size; }

        T* allocate(size_t amount) noexcept
        {
            assert(amount <= remaining());
            auto* result = storage.get() + size;
            size += amount;
            return result;
        }

        void clear() noexcept { size = 0; }
    };

    using Segments = std::conditional_t<ThreadSafe, SegmentedVector<Segment, 1, true>, std::vector<Segment>>;

    static constexpr size_t max_array_size() noexcept { return std::numeric_limits<size_t>::max() / ArraysPerSegment; }

    static size_t segment_size_for(size_t array_size)
    {
        if (array_size > max_array_size())
            throw std::length_error("RawArrayPool: array segment size exceeds addressable memory.");
        return ArraysPerSegment * array_size;
    }

    void increase_capacity()
    {
        if (m_cur_seg < m_segments.size() && m_array_size <= m_segments[m_cur_seg].remaining())
            return;

        if (m_cur_seg + 1 < m_segments.size())
        {
            m_cur_seg = m_cur_seg + 1;
            m_segments[m_cur_seg].clear();
            return;
        }

        m_segments.emplace_back(m_segment_size);

        m_cur_seg = m_segments.size() - 1;
    }

private:
    void ensure_index(size_t array_index) const
    {
        if (array_index >= size())
            throw std::out_of_range("RawArrayPool: index out of range.");
    }

    void ensure_not_empty() const
    {
        if (empty())
            throw std::out_of_range("RawArrayPool: container is empty.");
    }

public:
    explicit RawArrayPool(size_t array_size) : m_array_size(array_size), m_segment_size(segment_size_for(array_size)), m_cur_seg(0), m_size(0) {}

    uint_t insert(std::span<const T> value)
    {
        if (value.size() != m_array_size)
            throw std::invalid_argument("RawArrayPool: wrong number of elements.");

        return detail::with_lock<ThreadSafe>(m_writer_mutex,
                                             [&]
                                             {
                                                 const auto index = detail::load_size<ThreadSafe>(m_size);
                                                 if (index == std::numeric_limits<size_t>::max() || index > std::numeric_limits<uint_t>::max())
                                                     throw std::length_error("RawArrayPool: index is too large.");

                                                 if (m_array_size > 0)
                                                 {
                                                     increase_capacity();
                                                     auto* result = m_segments[m_cur_seg].allocate(m_array_size);
                                                     std::memcpy(result, value.data(), m_array_size * sizeof(T));
                                                 }

                                                 detail::store_size<ThreadSafe>(m_size, index + 1);
                                                 return static_cast<uint_t>(index);
                                             });
    }

    ConstView operator[](size_t array_index) const noexcept
    {
        assert(array_index < size());
        if (m_array_size == 0)
            return {};

        const size_t seg = array_index >> seg_shift;
        const size_t idx = array_index & seg_mask;
        return ConstView(m_segments[seg].storage.get() + idx * m_array_size, m_array_size);
    }

    ConstView at(size_t array_index) const
    {
        ensure_index(array_index);
        return (*this)[array_index];
    }

    ConstView front() const
    {
        ensure_not_empty();
        return (*this)[0];
    }

    ConstView back() const
    {
        ensure_not_empty();
        return (*this)[size() - 1];
    }

    void clear() noexcept
    {
        for (auto& segment : m_segments)
            segment.clear();
        m_cur_seg = 0;
        detail::store_size<ThreadSafe>(m_size, 0);
    }

    size_t memory_usage() const noexcept
    {
        size_t bytes = 0;
        for (const auto& seg : m_segments)
            bytes += seg.capacity * sizeof(T);
        return bytes;
    }

    size_t size() const noexcept { return detail::load_size<ThreadSafe>(m_size); }
    bool empty() const noexcept { return size() == 0; }
    size_t array_size() const noexcept { return m_array_size; }

private:
    Segments m_segments;

    size_t m_array_size;
    size_t m_segment_size;

    size_t m_cur_seg;
    detail::Size<ThreadSafe> m_size;
    [[no_unique_address]] detail::Mutex<ThreadSafe> m_writer_mutex;
};

}  // namespace ygg

#endif
