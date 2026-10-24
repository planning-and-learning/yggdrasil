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

#ifndef YGG_CONTAINERS_DETAIL_BIT_PACKED_LAYOUT_HPP_
#define YGG_CONTAINERS_DETAIL_BIT_PACKED_LAYOUT_HPP_

#include "yggdrasil/core/bit.hpp"

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace ygg::detail
{

template<std::unsigned_integral Block>
struct BitPackedLayout
{
private:
    using block_type = std::remove_const_t<Block>;

public:
    static constexpr size_t digits = std::numeric_limits<block_type>::digits;
    static_assert(std::has_single_bit(digits));
    static constexpr size_t block_shift = std::countr_zero(digits);

    struct Position
    {
        size_t block_index;
        uint8_t bit_offset;
    };

    static constexpr size_t block_count(size_t bits) noexcept { return bit::ceil_div(bits, digits); }

    static constexpr Position locate(size_t bit_index) noexcept { return { bit_index >> block_shift, static_cast<uint8_t>(bit_index & (digits - 1)) }; }

    static constexpr Position locate(size_t index, uint8_t width, uint8_t origin = 0) noexcept { return locate(static_cast<size_t>(origin) + index * width); }
};

}  // namespace ygg::detail

#endif
