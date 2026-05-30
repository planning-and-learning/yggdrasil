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

#ifndef YGG_COMMON_RAW_VECTOR_EQUAL_TO_HPP_
#define YGG_COMMON_RAW_VECTOR_EQUAL_TO_HPP_

#include "ygg/semantics/equal_to.hpp"
#include "ygg/containers/raw_vector_pool.hpp"

namespace ygg
{

template<std::unsigned_integral Size, TriviallyCopyable T>
struct EqualTo<RawVectorView<Size, T>>
{
    bool operator()(const RawVectorView<Size, T>& lhs, const RawVectorView<Size, T>& rhs) const noexcept { return equal_range(lhs, rhs); }
};

template<std::unsigned_integral Size, TriviallyCopyable T>
struct EqualTo<RawVectorView<const Size, const T>>
{
    bool operator()(const RawVectorView<const Size, const T>& lhs, const RawVectorView<const Size, const T>& rhs) const noexcept { return equal_range(lhs, rhs); }
};

}

#endif
