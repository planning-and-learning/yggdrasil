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

#ifndef YGG_CONTAINERS_SEGMENTED_VECTOR_EQUAL_TO_HPP_
#define YGG_CONTAINERS_SEGMENTED_VECTOR_EQUAL_TO_HPP_

#include "yggdrasil/containers/segmented_vector.hpp"
#include "yggdrasil/semantics/equal_to.hpp"

#include <cstddef>

namespace ygg
{

template<typename T, std::size_t FirstSegmentSize>
struct EqualTo<SegmentedVector<T, FirstSegmentSize>>
{
    bool operator()(const SegmentedVector<T, FirstSegmentSize>& lhs, const SegmentedVector<T, FirstSegmentSize>& rhs) const noexcept
    {
        if (lhs.size() != rhs.size())
            return false;

        for (std::size_t i = 0; i < lhs.size(); ++i)
        {
            if (!EqualTo<T> {}(lhs[i], rhs[i]))
                return false;
        }

        return true;
    }
};

}

#endif
