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

#ifndef YGG_CONTAINERS_DYNAMIC_BITSET_EQUAL_TO_HPP_
#define YGG_CONTAINERS_DYNAMIC_BITSET_EQUAL_TO_HPP_

#include "yggdrasil/containers/dynamic_bitset.hpp"
#include "yggdrasil/semantics/equal_to.hpp"

#include <concepts>

namespace ygg
{

template<typename Block, typename Allocator>
struct EqualTo<boost::dynamic_bitset<Block, Allocator>>
{
    using Type = boost::dynamic_bitset<Block, Allocator>;

    bool operator()(const Type& lhs, const Type& rhs) const { return lhs == rhs; }
};

template<std::unsigned_integral Block>
struct EqualTo<BitsetSpan<Block>>
{
    bool operator()(const BitsetSpan<Block>& lhs, const BitsetSpan<Block>& rhs) const noexcept { return lhs == rhs; }
};

}  // namespace ygg

#endif
