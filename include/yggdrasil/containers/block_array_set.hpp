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

#ifndef YGG_CONTAINERS_BLOCK_ARRAY_SET_HPP_
#define YGG_CONTAINERS_BLOCK_ARRAY_SET_HPP_

#include "yggdrasil/containers/block_array_pool.hpp"
#include "yggdrasil/containers/detail/basic_array_set.hpp"

#include <concepts>
#include <memory>

namespace ygg
{

/// ThreadSafe permits concurrent lookup, insertion, size queries, and reads
/// after publication was observed through size() or external synchronization.
/// Hash-table locking is limited to the target shard and pool appends use their
/// narrow publication lock. Clear, segment or memory inspection, move, and
/// destruction require quiescence.
template<std::unsigned_integral Block, bit::BlockCoder<Block> Coder = bit::ForwardingBlockCoder<Block>, size_t FirstSegmentSize = 16, bool ThreadSafe = false>
class BlockArraySet : public detail::BasicArraySet<BlockArrayPool<Block, Coder, FirstSegmentSize, ThreadSafe>>
{
private:
    using pool_type = BlockArrayPool<Block, Coder, FirstSegmentSize, ThreadSafe>;
    using Base = detail::BasicArraySet<pool_type>;

public:
    explicit BlockArraySet(size_t length) : Base(std::make_unique<pool_type>(length)) {}
};

}  // namespace ygg

#endif
