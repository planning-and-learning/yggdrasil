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

#ifndef YGG_SEMANTICS_CANONICALIZATION_HPP_
#define YGG_SEMANTICS_CANONICALIZATION_HPP_

#include <algorithm>
#include <cista/containers/optional.h>
#include <cista/containers/vector.h>
#include <ranges>

namespace ygg
{

/// A stored list (indices, records, variants of them): sorted, and deduplicated where asked.
template<bool Deduplicate = true, typename E>
bool is_canonical(const ::cista::offset::vector<E>& list)
{
    if (!std::is_sorted(list.begin(), list.end()))
        return false;
    if constexpr (Deduplicate)
        return std::adjacent_find(list.begin(), list.end()) == list.end();
    return true;
}

template<typename T>
bool is_canonical(const ::cista::optional<T>&)
{ return true; }

/// Any sequence that sorts and erases (std::vector, a sequence Store): sorted, and deduplicated where asked.
template<bool Deduplicate = true, typename List>
    requires std::ranges::random_access_range<List> && requires(List& list) { list.erase(list.begin(), list.end()); }
void canonicalize(List& list)
{
    if (!std::is_sorted(list.begin(), list.end()))
        std::sort(list.begin(), list.end());

    if constexpr (Deduplicate)
        list.erase(std::unique(list.begin(), list.end()), list.end());
}

/// The list, canonicalized.
template<bool Deduplicate = true, typename List>
    requires std::ranges::random_access_range<List> && requires(List& list) { list.erase(list.begin(), list.end()); }
List canonicalized(List list)
{
    canonicalize<Deduplicate>(list);
    return list;
}

template<bool Deduplicate = true, typename E>
void canonicalize(::cista::offset::vector<E>& list)
{
    if (!is_canonical<false>(list))
        std::sort(list.begin(), list.end());

    if constexpr (Deduplicate)
        list.erase(std::unique(list.begin(), list.end()), list.end());
}

template<typename T>
void canonicalize(::cista::optional<T>&)
{
}

}  // namespace ygg

#endif
