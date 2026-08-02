/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_CONTAINERS_DETAIL_GEOMETRIC_BYTE_STORAGE_HPP_
#define YGG_CONTAINERS_DETAIL_GEOMETRIC_BYTE_STORAGE_HPP_

#include "yggdrasil/containers/detail/geometric_segment_layout.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace ygg::detail
{

template<size_t FirstSegmentSize, size_t Alignment, bool PublishSegments>
class GeometricByteStorage
{
    using Layout = GeometricSegmentLayout<FirstSegmentSize>;

    struct Segment
    {
        std::byte* storage;
        size_t capacity;
        size_t used;

        static std::byte* allocate_storage(size_t size)
        {
            if constexpr (Alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
                return static_cast<std::byte*>(::operator new(size, std::align_val_t { Alignment }));
            else
                return static_cast<std::byte*>(::operator new(size));
        }

        static void free_storage(std::byte* storage) noexcept
        {
            if constexpr (Alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
                ::operator delete(storage, std::align_val_t { Alignment });
            else
                ::operator delete(storage);
        }

        explicit Segment(size_t capacity_) : storage(allocate_storage(capacity_)), capacity(capacity_), used(0) {}

        Segment(const Segment&) = delete;
        Segment& operator=(const Segment&) = delete;

        Segment(Segment&& other) noexcept : storage(std::exchange(other.storage, nullptr)), capacity(other.capacity), used(other.used) {}

        Segment& operator=(Segment&& other) noexcept
        {
            if (this != &other)
            {
                free_storage(storage);
                storage = std::exchange(other.storage, nullptr);
                capacity = other.capacity;
                used = other.used;
            }
            return *this;
        }

        ~Segment() { free_storage(storage); }

        size_t remaining() const noexcept { return capacity - used; }

        std::byte* allocate(size_t size) noexcept
        {
            assert(size <= remaining());
            auto* result = storage + used;
            used += size;
            return result;
        }

        void clear() noexcept { used = 0; }
    };

    struct NoPublishedSegments
    {
    };
    using PublishedSegmentPointers = std::conditional_t<PublishSegments, std::array<std::byte*, Layout::max_segments>, NoPublishedSegments>;

    size_t nominal_capacity(size_t segment) const
    {
        if (segment >= Layout::max_segments)
            throw std::length_error("GeometricByteStorage: segment is too large.");

        const auto units = Layout::segment_capacity(segment);
        if (m_unit_size > std::numeric_limits<size_t>::max() / units)
            throw std::length_error("GeometricByteStorage: segment is too large.");
        return units * m_unit_size;
    }

    size_t next_capacity(size_t needed) const
    {
        auto capacity = nominal_capacity(m_segments.size());
        if (capacity == 0)
            throw std::length_error("GeometricByteStorage: zero-sized segments cannot store data.");

        if (!m_segments.empty() && m_segments.back().capacity >= capacity)
        {
            const auto previous = m_segments.back().capacity;
            capacity = previous > std::numeric_limits<size_t>::max() / 2 ? std::max(previous, needed) : previous * 2;
        }

        while (capacity < needed)
        {
            if (capacity > std::numeric_limits<size_t>::max() / 2)
                return needed;
            capacity *= 2;
        }
        return capacity;
    }

    void publish(size_t segment) noexcept
    {
        if constexpr (PublishSegments)
            m_published[segment] = m_segments[segment].storage;
    }

    void rebuild_publication() noexcept
    {
        if constexpr (PublishSegments)
        {
            m_published.fill(nullptr);
            for (size_t segment = 0; segment < m_segments.size(); ++segment)
                publish(segment);
        }
    }

public:
    explicit GeometricByteStorage(size_t unit_size = 1) : m_unit_size(unit_size)
    {
        if (unit_size > 0)
            static_cast<void>(nominal_capacity(0));
    }

    GeometricByteStorage(const GeometricByteStorage&) = delete;
    GeometricByteStorage& operator=(const GeometricByteStorage&) = delete;

    GeometricByteStorage(GeometricByteStorage&& other) noexcept :
        m_segments(std::move(other.m_segments)),
        m_unit_size(other.m_unit_size),
        m_current_segment(std::exchange(other.m_current_segment, 0))
    {
        rebuild_publication();
    }

    GeometricByteStorage& operator=(GeometricByteStorage&& other) noexcept
    {
        if (this != &other)
        {
            m_segments = std::move(other.m_segments);
            m_unit_size = other.m_unit_size;
            m_current_segment = std::exchange(other.m_current_segment, 0);
            rebuild_publication();
        }
        return *this;
    }

    std::byte* allocate(size_t size)
    {
        assert(size > 0);
        while (m_current_segment < m_segments.size())
        {
            if (m_segments[m_current_segment].remaining() >= size)
                return m_segments[m_current_segment].allocate(size);
            ++m_current_segment;
        }

        const auto segment = m_segments.size();
        m_segments.emplace_back(next_capacity(size));
        publish(segment);
        m_current_segment = segment;
        return m_segments.back().allocate(size);
    }

private:
    std::byte* data(size_t segment) noexcept
    {
        if constexpr (PublishSegments)
        {
            assert(m_published[segment]);
            return m_published[segment];
        }
        else
        {
            return m_segments[segment].storage;
        }
    }

    const std::byte* data(size_t segment) const noexcept
    {
        if constexpr (PublishSegments)
        {
            assert(m_published[segment]);
            return m_published[segment];
        }
        else
        {
            return m_segments[segment].storage;
        }
    }

public:
    std::byte* data_at(size_t unit_index) noexcept
    {
        const auto segment = Layout::segment_index(unit_index);
        return data(segment) + Layout::segment_offset(unit_index, segment) * m_unit_size;
    }

    const std::byte* data_at(size_t unit_index) const noexcept
    {
        const auto segment = Layout::segment_index(unit_index);
        return data(segment) + Layout::segment_offset(unit_index, segment) * m_unit_size;
    }

    void clear() noexcept
    {
        for (auto& segment : m_segments)
            segment.clear();
        m_current_segment = 0;
    }

    size_t memory_usage() const noexcept
    {
        size_t bytes = 0;
        for (const auto& segment : m_segments)
            bytes += segment.capacity;
        return bytes;
    }

private:
    std::vector<Segment> m_segments;
    size_t m_unit_size;
    size_t m_current_segment = 0;
    [[no_unique_address]] PublishedSegmentPointers m_published {};
};

}  // namespace ygg::detail

#endif
