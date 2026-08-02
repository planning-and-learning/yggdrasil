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

#ifndef YGG_CONTAINERS_BIT_PACKED_ARRAY_SET_HPP_
#define YGG_CONTAINERS_BIT_PACKED_ARRAY_SET_HPP_

#include "yggdrasil/containers/bit_packed_array_pool.hpp"
#include "yggdrasil/containers/detail/basic_array_set.hpp"

#include <concepts>
#include <cstdint>
#include <memory>

namespace ygg
{

/// ThreadSafe permits concurrent lookup, insertion, size queries, and reads
/// after publication was observed through size() or external synchronization.
/// Hash-table locking is limited to the target shard and pool appends use their
/// narrow publication lock. Clear, segment or memory inspection, move, and
/// destruction require quiescence. Atomic packed access prevents
/// neighboring-array races but does not make multi-block updates atomic.
template<std::unsigned_integral Block, bit::BlockCoder<Block> Coder = bit::ForwardingBlockCoder<Block>, size_t FirstSegmentSize = 16, bool ThreadSafe = false>
class BitPackedArraySet : public detail::BasicArraySet<BitPackedArrayPool<Block, Coder, FirstSegmentSize, ThreadSafe>>
{
private:
    using pool_type = BitPackedArrayPool<Block, Coder, FirstSegmentSize, ThreadSafe>;
    using Base = detail::BasicArraySet<pool_type>;

public:
    BitPackedArraySet(size_t length, uint8_t width) : Base(std::make_unique<pool_type>(length, width)) {}

    uint8_t width() const noexcept { return this->storage().width(); }
};

}  // namespace ygg

#endif
