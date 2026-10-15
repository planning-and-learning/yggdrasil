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

#ifndef YGG_SEMANTICS_CONTAINERS_PACKED_OBJECT_ROW_ORDERING_HPP_
#define YGG_SEMANTICS_CONTAINERS_PACKED_OBJECT_ROW_ORDERING_HPP_

#include "yggdrasil/containers/packed_object_row.hpp"
#include "yggdrasil/semantics/comparators.hpp"

namespace ygg
{

template<typename ObjectIndex>
struct Less<PackedObjectRow<ObjectIndex>>
{
    using T = PackedObjectRow<ObjectIndex>;

    bool operator()(const T& lhs, const T& rhs) const
        requires OrderedByLess<ObjectIndex>
    {
        return less_range(lhs, rhs);
    }
};

template<typename ObjectIndex, typename C>
struct Less<View<PackedObjectRow<ObjectIndex>, C>>
{
    using T = View<PackedObjectRow<ObjectIndex>, C>;

    bool operator()(const T& lhs, const T& rhs) const
        requires OrderedByLess<std::ranges::range_value_t<const T>>
    {
        return less_range(lhs, rhs);
    }
};

}

#endif
