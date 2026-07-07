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

#ifndef YGG_CONTAINERS_DYNAMIC_BITSET_HASH_HPP_
#define YGG_CONTAINERS_DYNAMIC_BITSET_HASH_HPP_

#include "yggdrasil/containers/dynamic_bitset.hpp"
#include "yggdrasil/semantics/hash.hpp"

#include <concepts>
#include <cstddef>
#include <iterator>
#include <vector>

namespace ygg
{

template<typename Block, typename Allocator>
struct Hash<boost::dynamic_bitset<Block, Allocator>>
{
    using Type = boost::dynamic_bitset<Block, Allocator>;

    size_t operator()(const Type& bitset) const
    {
        auto blocks = std::vector<Block>();
        blocks.reserve(bitset.num_blocks());
        boost::to_block_range(bitset, std::back_inserter(blocks));

        size_t seed = bitset.size();
        for (const auto& block : blocks)
            ygg::hash_combine(seed, block);
        return seed;
    }
};

template<std::unsigned_integral Block>
struct Hash<BitsetSpan<Block>>
{
    size_t operator()(const BitsetSpan<Block>& bitset_span) const noexcept
    {
        size_t aggregated_hash = bitset_span.num_bits();
        for (const auto& block : bitset_span.blocks())
            ygg::hash_combine(aggregated_hash, block);
        return aggregated_hash;
    }
};

}  // namespace ygg

#endif
