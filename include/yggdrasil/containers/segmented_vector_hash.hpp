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

#ifndef YGG_COMMON_SEGMENTED_VECTOR_HASH_HPP_
#define YGG_COMMON_SEGMENTED_VECTOR_HASH_HPP_

#include "yggdrasil/containers/segmented_vector.hpp"
#include "yggdrasil/semantics/hash.hpp"

#include <cstddef>

namespace ygg
{

template<typename T, std::size_t FirstSegmentSize>
struct Hash<SegmentedVector<T, FirstSegmentSize>>
{
    size_t operator()(const SegmentedVector<T, FirstSegmentSize>& value) const noexcept
    {
        size_t seed = value.size();
        for (std::size_t i = 0; i < value.size(); ++i)
            ygg::hash_combine(seed, value[i]);
        return seed;
    }
};

}

#endif
