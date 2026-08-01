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

#ifndef YGG_CONTAINERS_DETAIL_GEOMETRIC_SEGMENT_LAYOUT_HPP_
#define YGG_CONTAINERS_DETAIL_GEOMETRIC_SEGMENT_LAYOUT_HPP_

#include <bit>
#include <concepts>
#include <cstddef>
#include <limits>

namespace ygg::detail
{

template<size_t FirstSegmentSize, std::unsigned_integral Index = size_t>
struct GeometricSegmentLayout
{
    static_assert(std::has_single_bit(FirstSegmentSize));

    static constexpr size_t shift = std::countr_zero(FirstSegmentSize);
    static constexpr size_t max_segments = std::numeric_limits<Index>::digits - shift;

    static constexpr size_t segment_index(Index index) noexcept
    {
        const auto quotient = index >> shift;
        if constexpr (shift == 0)
            return quotient == std::numeric_limits<Index>::max() ? max_segments : std::bit_width(static_cast<Index>(quotient + Index { 1 })) - 1;
        else
            return std::bit_width(static_cast<Index>(quotient + Index { 1 })) - 1;
    }

    static constexpr size_t segment_offset(Index index, size_t segment) noexcept
    {
        const auto quotient = index >> shift;
        const auto remainder = index & static_cast<Index>(FirstSegmentSize - 1);
        return static_cast<size_t>(((quotient - ((Index { 1 } << segment) - 1)) << shift) + remainder);
    }

    static constexpr size_t segment_capacity(size_t segment) noexcept { return FirstSegmentSize << segment; }
};

}  // namespace ygg::detail

#endif
