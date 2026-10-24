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

#ifndef YGG_SEMANTICS_CONTAINERS_BLOCK_ARRAY_HASH_HPP_
#define YGG_SEMANTICS_CONTAINERS_BLOCK_ARRAY_HASH_HPP_

#include "yggdrasil/containers/array.hpp"
#include "yggdrasil/semantics/hash.hpp"

namespace ygg
{

template<std::unsigned_integral Block, bit::BlockCoder<std::remove_const_t<Block>> Coder, bool ThreadSafe>
struct Hash<BasicBitPackedArrayView<Block, Coder, ThreadSafe>>
{
    hash_t operator()(const BasicBitPackedArrayView<Block, Coder, ThreadSafe>& value) const
        requires Hashable<std::ranges::range_value_t<const BasicBitPackedArrayView<Block, Coder, ThreadSafe>>>
    { return ygg::hash_range(value); }
};

template<std::unsigned_integral Block, bit::BlockCoder<std::remove_const_t<Block>> Coder, bool ThreadSafe, typename C>
struct Hash<View<BasicBitPackedArrayView<Block, Coder, ThreadSafe>, C>>
{
    hash_t operator()(const View<BasicBitPackedArrayView<Block, Coder, ThreadSafe>, C>& value) const
        requires Hashable<std::ranges::range_value_t<const View<BasicBitPackedArrayView<Block, Coder, ThreadSafe>, C>>>
    { return ygg::hash_range(value); }
};

template<std::unsigned_integral Block, bit::BlockCoder<std::remove_const_t<Block>> Coder>
struct Hash<BasicBlockArrayView<Block, Coder>>
{
    hash_t operator()(const BasicBlockArrayView<Block, Coder>& value) const
        requires Hashable<std::ranges::range_value_t<const BasicBlockArrayView<Block, Coder>>>
    { return ygg::hash_range(value); }
};

template<std::unsigned_integral Block, bit::BlockCoder<std::remove_const_t<Block>> Coder, typename C>
struct Hash<View<BasicBlockArrayView<Block, Coder>, C>>
{
    hash_t operator()(const View<BasicBlockArrayView<Block, Coder>, C>& value) const
        requires Hashable<std::ranges::range_value_t<const View<BasicBlockArrayView<Block, Coder>, C>>>
    { return ygg::hash_range(value); }
};

}

#endif
