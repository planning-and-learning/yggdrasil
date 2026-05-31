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

#include "yggdrasil/semantics/comparators.hpp"
#include "yggdrasil/semantics/equal_to.hpp"
#include "yggdrasil/core/types.hpp"

#include <cista/containers/optional.h>

#include <fmt/format.h>

#include <algorithm>
#include <concepts>
#include <string>
#include <type_traits>

namespace ygg
{

namespace detail
{
template<typename C, typename Element>
std::string fmt_key(const C& context, const Element& element)
{
    const auto view = make_view(element, context);
    return fmt::format("{}", make_view(view.get_data(), view.get_context()));
}

}

template<typename T>
bool is_canonical(const IndexList<T>& list)
{
    return std::is_sorted(list.begin(), list.end(), Less<Index<T>> {});
}

template<typename T>
bool is_canonical(const DataList<T>& list)
{
    return std::is_sorted(list.begin(), list.end(), Less<Data<T>> {});
}

template<typename T>
bool is_canonical(const ::cista::optional<T>& element)
{
    return true;
}

template<typename T>
void canonicalize(IndexList<T>& list)
{
    if (!is_canonical(list))
        std::sort(list.begin(), list.end(), Less<Index<T>> {});

    list.erase(std::unique(list.begin(), list.end(), EqualTo<Index<T>> {}), list.end());
}

template<typename T>
void canonicalize(DataList<T>& list)
{
    if (!is_canonical(list))
        std::sort(list.begin(), list.end(), Less<Data<T>> {});

    list.erase(std::unique(list.begin(), list.end(), EqualTo<Data<T>> {}), list.end());
}

template<typename T>
void canonicalize(::cista::optional<T>& element)
{
}

template<typename C, typename T>
bool is_canonical(const C& context, const IndexList<T>& list)
{
    auto less = [&](const auto& lhs, const auto& rhs) { return detail::fmt_key(context, lhs) < detail::fmt_key(context, rhs); };
    return std::is_sorted(list.begin(), list.end(), less);
}

template<typename C, typename T>
bool is_canonical(const C& context, const DataList<T>& list)
{
    auto less = [&](const auto& lhs, const auto& rhs) { return detail::fmt_key(context, lhs) < detail::fmt_key(context, rhs); };
    return std::is_sorted(list.begin(), list.end(), less);
}

template<typename C, typename T>
void canonicalize(const C& context, IndexList<T>& list)
{
    auto less = [&](const auto& lhs, const auto& rhs) { return detail::fmt_key(context, lhs) < detail::fmt_key(context, rhs); };
    auto equal = [&](const auto& lhs, const auto& rhs) { return detail::fmt_key(context, lhs) == detail::fmt_key(context, rhs); };

    std::sort(list.begin(), list.end(), less);
    list.erase(std::unique(list.begin(), list.end(), equal), list.end());
}

template<typename C, typename T>
void canonicalize(const C& context, DataList<T>& list)
{
    auto less = [&](const auto& lhs, const auto& rhs) { return detail::fmt_key(context, lhs) < detail::fmt_key(context, rhs); };
    auto equal = [&](const auto& lhs, const auto& rhs) { return detail::fmt_key(context, lhs) == detail::fmt_key(context, rhs); };

    std::sort(list.begin(), list.end(), less);
    list.erase(std::unique(list.begin(), list.end(), equal), list.end());
}

}

#endif
