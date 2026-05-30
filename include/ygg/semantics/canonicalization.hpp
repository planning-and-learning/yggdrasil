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

#ifndef YGG_COMMON_CANONICALIZATION_HPP_
#define YGG_COMMON_CANONICALIZATION_HPP_

#include "ygg/semantics/comparators.hpp"
#include "ygg/semantics/equal_to.hpp"
#include "ygg/core/types.hpp"

#include <cista/containers/optional.h>

#include <algorithm>
#include <concepts>
#include <type_traits>

namespace ygg
{

namespace detail
{
template<typename List>
bool is_canonical_list(const List& list)
{
    using Element = std::remove_cvref_t<decltype(*list.begin())>;
    return std::is_sorted(list.begin(), list.end(), Less<Element> {});
}

template<typename List>
void canonicalize_list(List& list)
{
    using Element = std::remove_cvref_t<decltype(*list.begin())>;

    if (!is_canonical_list(list))
        std::sort(list.begin(), list.end(), Less<Element> {});

    list.erase(std::unique(list.begin(), list.end(), EqualTo<Element> {}), list.end());
}
}

template<typename T>
bool is_canonical(const IndexList<T>& list)
{
    return detail::is_canonical_list(list);
}

template<typename T>
bool is_canonical(const DataList<T>& list)
{
    return detail::is_canonical_list(list);
}

template<typename T>
bool is_canonical(const ::cista::optional<T>& element)
{
    return true;
}

template<typename T>
void canonicalize(IndexList<T>& list)
{
    detail::canonicalize_list(list);
}

template<typename T>
void canonicalize(DataList<T>& list)
{
    detail::canonicalize_list(list);
}

template<typename T>
void canonicalize(::cista::optional<T>& element)
{
}

template<typename T>
concept Canonicalizable = requires(T value, const T const_value) {
    { is_canonical(const_value) } -> std::same_as<bool>;
    { canonicalize(value) };
};

template<typename Tag>
concept CanonicalDataTag = Canonicalizable<Data<Tag>>;

}

#endif
