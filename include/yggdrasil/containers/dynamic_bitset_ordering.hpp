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

#ifndef YGG_COMMON_DYNAMIC_BITSET_ORDERING_HPP_
#define YGG_COMMON_DYNAMIC_BITSET_ORDERING_HPP_

#include "yggdrasil/containers/dynamic_bitset.hpp"
#include "yggdrasil/semantics/comparators.hpp"

#include <concepts>
#include <iterator>
#include <vector>

namespace ygg {

template <typename Block, typename Allocator>
struct Less<boost::dynamic_bitset<Block, Allocator>> {
  using Type = boost::dynamic_bitset<Block, Allocator>;

  bool operator()(const Type &lhs, const Type &rhs) const {
    if (lhs.size() != rhs.size())
      return lhs.size() < rhs.size();

    auto lhs_blocks = std::vector<Block>();
    auto rhs_blocks = std::vector<Block>();
    lhs_blocks.reserve(lhs.num_blocks());
    rhs_blocks.reserve(rhs.num_blocks());
    boost::to_block_range(lhs, std::back_inserter(lhs_blocks));
    boost::to_block_range(rhs, std::back_inserter(rhs_blocks));

    return less_range(lhs_blocks, rhs_blocks);
  }
};

template <std::unsigned_integral Block> struct Less<BitsetSpan<Block>> {
  bool operator()(const BitsetSpan<Block> &lhs,
                  const BitsetSpan<Block> &rhs) const noexcept {
    assert(lhs.trailing_bits_zero());
    assert(rhs.trailing_bits_zero());

    if (lhs.num_bits() != rhs.num_bits())
      return lhs.num_bits() < rhs.num_bits();

    return less_range(lhs.blocks(), rhs.blocks());
  }
};

} // namespace ygg

#endif
