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

template<typename Block, typename Coder>
struct EqualTo<BasicBitPackedArrayView<Block, Coder>>
{
    using Type = BasicBitPackedArrayView<Block, Coder>;

    bool operator()(const Type& lhs, const Type& rhs) const noexcept { return equal_range(lhs, rhs); }
};

template<typename Block, typename Coder, typename C>
struct EqualTo<View<BasicBitPackedArrayView<Block, Coder>, C>>
{
    using Type = View<BasicBitPackedArrayView<Block, Coder>, C>;

    bool operator()(const Type& lhs, const Type& rhs) const noexcept { return equal_range(lhs, rhs); }
};

template<typename Block, typename Coder>
struct EqualTo<BasicBlockArrayView<Block, Coder>>
{
    using Type = BasicBlockArrayView<Block, Coder>;

    bool operator()(const Type& lhs, const Type& rhs) const noexcept { return equal_range(lhs, rhs); }
};

template<typename Block, typename Coder, typename C>
struct EqualTo<View<BasicBlockArrayView<Block, Coder>, C>>
{
    using Type = View<BasicBlockArrayView<Block, Coder>, C>;

    bool operator()(const Type& lhs, const Type& rhs) const noexcept { return equal_range(lhs, rhs); }
};

}

#endif
