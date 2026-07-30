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

#ifndef YGG_SEMANTICS_CONTAINERS_DYNAMIC_BITSET_HASH_HPP_
#define YGG_SEMANTICS_CONTAINERS_DYNAMIC_BITSET_HASH_HPP_

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

    hash_t operator()(const Type& bitset) const
    {
        auto blocks = std::vector<Block>();
        blocks.reserve(bitset.num_blocks());
        boost::to_block_range(bitset, std::back_inserter(blocks));

        const auto block_span = std::span<const Block>(blocks);
        hash_t seed = bitset.size();
        for (size_t i = 0; i < detail::num_canonical_bit_blocks(bitset.size()); ++i)
            ygg::hash_combine(seed, detail::canonical_bit_block(block_span, bitset.size(), i));
        return seed;
    }
};

template<std::unsigned_integral Block>
struct Hash<BitsetSpan<Block>>
{
    hash_t operator()(const BitsetSpan<Block>& bitset_span) const noexcept
    {
        const auto blocks = bitset_span.blocks();
        hash_t aggregated_hash = bitset_span.num_bits();
        for (size_t i = 0; i < detail::num_canonical_bit_blocks(bitset_span.num_bits()); ++i)
            ygg::hash_combine(aggregated_hash, detail::canonical_bit_block(blocks, bitset_span.num_bits(), i));
        return aggregated_hash;
    }
};

}  // namespace ygg

#endif
