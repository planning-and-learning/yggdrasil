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

#ifndef YGG_SEMANTICS_CONTAINERS_BLOCK_ARRAY_EQUAL_TO_HPP_
#define YGG_SEMANTICS_CONTAINERS_BLOCK_ARRAY_EQUAL_TO_HPP_

#include "yggdrasil/containers/array.hpp"
#include "yggdrasil/semantics/equal_to.hpp"

namespace ygg
{

template<std::unsigned_integral Block, bit::BlockCoder<std::remove_const_t<Block>> Coder, bool ThreadSafe>
struct EqualTo<BasicBitPackedArrayView<Block, Coder, ThreadSafe>>
{
    bool operator()(const BasicBitPackedArrayView<Block, Coder, ThreadSafe>& lhs, const BasicBitPackedArrayView<Block, Coder, ThreadSafe>& rhs) const
        requires EqualityComparableByEqualTo<std::ranges::range_value_t<const BasicBitPackedArrayView<Block, Coder, ThreadSafe>>>
    { return equal_range(lhs, rhs); }
};

template<std::unsigned_integral Block, bit::BlockCoder<std::remove_const_t<Block>> Coder, bool ThreadSafe, typename C>
struct EqualTo<View<BasicBitPackedArrayView<Block, Coder, ThreadSafe>, C>>
{
    bool operator()(const View<BasicBitPackedArrayView<Block, Coder, ThreadSafe>, C>& lhs,
                    const View<BasicBitPackedArrayView<Block, Coder, ThreadSafe>, C>& rhs) const
        requires EqualityComparableByEqualTo<std::ranges::range_value_t<const View<BasicBitPackedArrayView<Block, Coder, ThreadSafe>, C>>>
    { return equal_range(lhs, rhs); }
};

template<std::unsigned_integral Block, bit::BlockCoder<std::remove_const_t<Block>> Coder>
struct EqualTo<BasicBlockArrayView<Block, Coder>>
{
    bool operator()(const BasicBlockArrayView<Block, Coder>& lhs, const BasicBlockArrayView<Block, Coder>& rhs) const
        requires EqualityComparableByEqualTo<std::ranges::range_value_t<const BasicBlockArrayView<Block, Coder>>>
    { return equal_range(lhs, rhs); }
};

template<std::unsigned_integral Block, bit::BlockCoder<std::remove_const_t<Block>> Coder, typename C>
struct EqualTo<View<BasicBlockArrayView<Block, Coder>, C>>
{
    bool operator()(const View<BasicBlockArrayView<Block, Coder>, C>& lhs, const View<BasicBlockArrayView<Block, Coder>, C>& rhs) const
        requires EqualityComparableByEqualTo<std::ranges::range_value_t<const View<BasicBlockArrayView<Block, Coder>, C>>>
    { return equal_range(lhs, rhs); }
};

}

#endif
