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

#ifndef YGG_CONTAINERS_DETAIL_SEGMENTED_ROWS_HPP_
#define YGG_CONTAINERS_DETAIL_SEGMENTED_ROWS_HPP_

#include "yggdrasil/containers/detail/geometric_segment_layout.hpp"
#include "yggdrasil/containers/detail/threading.hpp"

#include <cassert>
#include <concepts>
#include <cstddef>
#include <span>
#include <stdexcept>

namespace ygg::detail
{

template<bool ThreadSafe>
inline const Size<ThreadSafe> empty_segmented_size {};

/// Reads the elements of a SegmentedVector through its segment table and size, so a holder can keep the vector
/// itself behind a pointer and still read inline. The vector must stay at one address while it is read this way;
/// the ThreadSafe publication guarantees of SegmentedVector apply unchanged.
template<typename T, size_t FirstSegmentSize, bool ThreadSafe, std::unsigned_integral Index>
class SegmentedRows
{
private:
    using Layout = GeometricSegmentLayout<FirstSegmentSize, Index>;

    const Pointer<ThreadSafe, void>* m_segments;
    const Size<ThreadSafe>* m_size;

    const T* element(size_t pos) const noexcept
    {
        const auto index = Layout::segment_index(static_cast<Index>(pos));
        const auto* segment = static_cast<const T*>(load_pointer<ThreadSafe, void>(std::span(m_segments, Layout::max_segments)[index]));
        return &std::span(segment, Layout::segment_capacity(index))[Layout::segment_offset(static_cast<Index>(pos), index)];
    }

    void ensure_not_empty() const
    {
        if (empty())
            throw std::out_of_range("SegmentedVector: container is empty.");
    }

public:
    SegmentedRows() noexcept : m_segments(nullptr), m_size(&empty_segmented_size<ThreadSafe>) {}
    SegmentedRows(const Pointer<ThreadSafe, void>* segments, const Size<ThreadSafe>* size) noexcept : m_segments(segments), m_size(size) {}

    const T& operator[](size_t pos) const noexcept
    {
        assert(pos < size());
        return *element(pos);
    }

    const T& at(size_t pos) const
    {
        if (pos >= size())
            throw std::out_of_range("SegmentedVector::at");
        return *element(pos);
    }

    const T& front() const
    {
        ensure_not_empty();
        return *element(0);
    }

    const T& back() const
    {
        ensure_not_empty();
        return *element(size() - 1);
    }

    size_t size() const noexcept { return load_size<ThreadSafe>(*m_size); }
    bool empty() const noexcept { return size() == 0; }
};

}  // namespace ygg::detail

#endif
