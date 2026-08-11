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

#include "yggdrasil/core/types.hpp"
#include "yggdrasil/semantics/comparison.hpp"

#include <algorithm>
#include <cista/containers/optional.h>
#include <concepts>
#include <fmt/format.h>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ygg
{

namespace detail
{
template<typename C, typename Element>
    requires ViewConcept<Element, C>
std::string fmt_key(const C& context, const Element& element)
{
    return fmt::format("{}", make_view(element, context));
}

template<bool Deduplicate, typename C, typename List>
    requires ViewConcept<typename List::value_type, C>
bool is_canonical(const C& context, const List& list)
{
    if (list.size() < 2)
        return true;

    auto previous_key = fmt_key(context, list[0]);
    for (std::size_t i = 1; i < list.size(); ++i)
    {
        auto current_key = fmt_key(context, list[i]);
        if constexpr (Deduplicate)
        {
            if (!(previous_key < current_key))
                return false;
        }
        else if (current_key < previous_key || (current_key == previous_key && list[i] < list[i - 1]))
            return false;
        previous_key = std::move(current_key);
    }
    return true;
}

template<bool Deduplicate, typename C, typename List>
    requires ViewConcept<typename List::value_type, C>
void canonicalize(const C& context, List& list)
{
    using Element = typename List::value_type;

    if (list.size() < 2)
        return;

    auto keyed = std::vector<std::pair<std::string, Element>> {};
    keyed.reserve(list.size());
    for (auto& element : list)
        keyed.emplace_back(fmt_key(context, element), std::move(element));

    if (!std::is_sorted(keyed.begin(), keyed.end()))
        std::sort(keyed.begin(), keyed.end());

    auto size = std::size_t { 0 };
    for (std::size_t i = 0; i < keyed.size(); ++i)
    {
        if constexpr (Deduplicate)
            if (i > 0 && keyed[i - 1].first == keyed[i].first)
                continue;
        list[size++] = std::move(keyed[i].second);
    }
    list.resize(size);
}

}

template<bool Deduplicate = true, typename T>
bool is_canonical(const IndexList<T>& list)
{
    if (!std::is_sorted(list.begin(), list.end()))
        return false;
    if constexpr (Deduplicate)
        return std::adjacent_find(list.begin(), list.end()) == list.end();
    return true;
}

template<bool Deduplicate = true, typename T>
bool is_canonical(const DataList<T>& list)
{
    if (!std::is_sorted(list.begin(), list.end()))
        return false;
    if constexpr (Deduplicate)
        return std::adjacent_find(list.begin(), list.end()) == list.end();
    return true;
}

template<typename T>
bool is_canonical(const ::cista::optional<T>&)
{
    return true;
}

template<bool Deduplicate = true, typename T>
void canonicalize(IndexList<T>& list)
{
    if (!is_canonical<false>(list))
        std::sort(list.begin(), list.end());

    if constexpr (Deduplicate)
        list.erase(std::unique(list.begin(), list.end()), list.end());
}

template<bool Deduplicate = true, typename T>
void canonicalize(DataList<T>& list)
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

template<bool Deduplicate = true, typename C, typename T>
bool is_canonical(const C& context, const IndexList<T>& list)
{
    return detail::is_canonical<Deduplicate>(context, list);
}

template<bool Deduplicate = true, typename C, typename T>
bool is_canonical(const C& context, const DataList<T>& list)
{
    return detail::is_canonical<Deduplicate>(context, list);
}

template<bool Deduplicate = true, typename C, typename T>
void canonicalize(const C& context, IndexList<T>& list)
{
    detail::canonicalize<Deduplicate>(context, list);
}

template<bool Deduplicate = true, typename C, typename T>
void canonicalize(const C& context, DataList<T>& list)
{
    detail::canonicalize<Deduplicate>(context, list);
}

}

#endif
