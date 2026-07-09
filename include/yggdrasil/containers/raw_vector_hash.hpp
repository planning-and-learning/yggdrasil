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

#ifndef YGG_CONTAINERS_RAW_VECTOR_HASH_HPP_
#define YGG_CONTAINERS_RAW_VECTOR_HASH_HPP_

#include "yggdrasil/containers/raw_vector_pool.hpp"
#include "yggdrasil/semantics/hash.hpp"

namespace ygg
{

template<std::unsigned_integral Size, TriviallyCopyable T>
struct Hash<RawVectorView<Size, T>>
{
    hash_t operator()(const RawVectorView<Size, T>& value) const noexcept { return ygg::hash_range(value); }
};

template<std::unsigned_integral Size, TriviallyCopyable T>
struct Hash<RawVectorView<const Size, const T>>
{
    hash_t operator()(const RawVectorView<const Size, const T>& value) const noexcept { return ygg::hash_range(value); }
};

}

#endif
