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

#ifndef YGG_COMMON_BLOCK_ARRAY_HASH_HPP_
#define YGG_COMMON_BLOCK_ARRAY_HASH_HPP_

#include "ygg/containers/array.hpp"
#include "ygg/semantics/hash.hpp"

namespace ygg
{

template<typename Block, typename Coder>
struct Hash<BasicBitPackedArrayView<Block, Coder>>
{
    using Type = BasicBitPackedArrayView<Block, Coder>;

    size_t operator()(const Type& value) const noexcept { return hash_range(value); }
};

template<typename Block, typename Coder, typename C>
struct Hash<View<BasicBitPackedArrayView<Block, Coder>, C>>
{
    using Type = View<BasicBitPackedArrayView<Block, Coder>, C>;

    size_t operator()(const Type& value) const noexcept { return hash_range(value); }
};

template<typename Block, typename Coder>
struct Hash<BasicBlockArrayView<Block, Coder>>
{
    using Type = BasicBlockArrayView<Block, Coder>;

    size_t operator()(const Type& value) const noexcept { return hash_range(value); }
};

template<typename Block, typename Coder, typename C>
struct Hash<View<BasicBlockArrayView<Block, Coder>, C>>
{
    using Type = View<BasicBlockArrayView<Block, Coder>, C>;

    size_t operator()(const Type& value) const noexcept { return hash_range(value); }
};

}

#endif
