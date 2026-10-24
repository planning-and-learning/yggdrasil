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

#ifndef YGG_SERIALIZATION_CISTA_ORDERING_HPP_
#define YGG_SERIALIZATION_CISTA_ORDERING_HPP_

#include "yggdrasil/containers/array.hpp"
#include "yggdrasil/containers/optional.hpp"
#include "yggdrasil/containers/pair.hpp"
#include "yggdrasil/containers/variant.hpp"
#include "yggdrasil/containers/vector.hpp"
#include "yggdrasil/semantics/comparators.hpp"

#include <cista/containers/array.h>
#include <cista/containers/optional.h>
#include <cista/containers/pair.h>
#include <cista/containers/string.h>
#include <cista/containers/variant.h>
#include <cista/containers/vector.h>
#include <type_traits>

namespace ygg
{

template<typename C, typename T, size_t N>
struct Less<View<::cista::array<T, N>, C>>
{
    bool operator()(const View<::cista::array<T, N>, C>& lhs, const View<::cista::array<T, N>, C>& rhs) const
        requires OrderedByLess<std::ranges::range_value_t<const View<::cista::array<T, N>, C>>>
    { return less_range(lhs, rhs); }
};

template<typename T1, typename T2>
struct Less<::cista::pair<T1, T2>>
{
    constexpr bool operator()(const ::cista::pair<T1, T2>& lhs, const ::cista::pair<T1, T2>& rhs) const
        requires OrderedByLess<T1> && OrderedByLess<T2>
    {
        if (Less<std::remove_cvref_t<T1>> {}(lhs.first, rhs.first))
            return true;
        if (Less<std::remove_cvref_t<T1>> {}(rhs.first, lhs.first))
            return false;
        return Less<std::remove_cvref_t<T2>> {}(lhs.second, rhs.second);
    }
};

template<typename C, typename T1, typename T2>
struct Less<View<::cista::pair<T1, T2>, C>>
{
    bool operator()(const View<::cista::pair<T1, T2>, C>& lhs, const View<::cista::pair<T1, T2>, C>& rhs) const
        requires requires {
            { lhs.get_first() } -> OrderedByLess;
            { lhs.get_second() } -> OrderedByLess;
        }
    {
        using First = ViewOrValue<T1, C>;
        if (Less<First> {}(lhs.get_first(), rhs.get_first()))
            return true;
        if (Less<First> {}(rhs.get_first(), lhs.get_first()))
            return false;
        return Less<ViewOrValue<T2, C>> {}(lhs.get_second(), rhs.get_second());
    }
};

template<typename Ptr>
struct Less<::cista::basic_string<Ptr>>
{
    bool operator()(const ::cista::basic_string<Ptr>& lhs, const ::cista::basic_string<Ptr>& rhs) const noexcept { return less_range(lhs, rhs); }
};

template<typename T, template<typename> typename Ptr, bool IndexPointers, typename TemplateSizeType, class Allocator>
struct Less<::cista::basic_vector<T, Ptr, IndexPointers, TemplateSizeType, Allocator>>
{
    bool operator()(const ::cista::basic_vector<T, Ptr, IndexPointers, TemplateSizeType, Allocator>& lhs,
                    const ::cista::basic_vector<T, Ptr, IndexPointers, TemplateSizeType, Allocator>& rhs) const
        requires OrderedByLess<T>
    { return less_range(lhs, rhs); }
};

template<typename C, typename T, template<typename> typename Ptr, bool IndexPointers, typename TemplateSizeType, class Allocator>
struct Less<View<::cista::basic_vector<T, Ptr, IndexPointers, TemplateSizeType, Allocator>, C>>
{
    bool operator()(const View<::cista::basic_vector<T, Ptr, IndexPointers, TemplateSizeType, Allocator>, C>& lhs,
                    const View<::cista::basic_vector<T, Ptr, IndexPointers, TemplateSizeType, Allocator>, C>& rhs) const
        requires OrderedByLess<std::ranges::range_value_t<const View<::cista::basic_vector<T, Ptr, IndexPointers, TemplateSizeType, Allocator>, C>>>
    { return less_range(lhs, rhs); }
};

template<typename T>
struct Less<::cista::optional<T>>
{
    bool operator()(const ::cista::optional<T>& lhs, const ::cista::optional<T>& rhs) const
        requires OrderedByLess<T>
    {
        if (lhs.has_value() != rhs.has_value())
            return !lhs.has_value();

        return lhs.has_value() ? Less<std::remove_cvref_t<T>> {}(*lhs, *rhs) : false;
    }
};

template<typename C, typename T>
struct Less<View<::cista::optional<T>, C>>
{
    bool operator()(const View<::cista::optional<T>, C>& lhs, const View<::cista::optional<T>, C>& rhs) const
        requires requires {
            { lhs.value() } -> OrderedByLess;
        }
    {
        if (lhs.has_value() != rhs.has_value())
            return !lhs.has_value();

        return lhs.has_value() ? Less<ViewOrValue<T, C>> {}(lhs.value(), rhs.value()) : false;
    }
};

template<typename... Ts>
struct Less<::cista::offset::variant<Ts...>>
{
    constexpr bool operator()(const ::cista::offset::variant<Ts...>& lhs, const ::cista::offset::variant<Ts...>& rhs) const
        requires(OrderedByLess<Ts> && ...)
    {
        if (lhs.valid() != rhs.valid())
            return !lhs.valid();

        if (!lhs.valid())
            return false;

        if (lhs.index() != rhs.index())
            return lhs.index() < rhs.index();

        return lhs.apply(
            [&]<typename L>(const L& l)
            {
                return rhs.apply(
                    [&]<typename R>(const R& r)
                    {
                        if constexpr (std::is_same_v<std::remove_cvref_t<L>, std::remove_cvref_t<R>>)
                            return Less<std::remove_cvref_t<L>> {}(l, r);
                        return false;
                    });
            });
    }
};

template<typename C, typename... Ts>
struct Less<View<::cista::offset::variant<Ts...>, C>>
{
    bool operator()(const View<::cista::offset::variant<Ts...>, C>& lhs, const View<::cista::offset::variant<Ts...>, C>& rhs) const
        requires(OrderedByLess<ViewOrValue<Ts, C>> && ...)
    {
        if (lhs.valid() != rhs.valid())
            return !lhs.valid();

        if (!lhs.valid())
            return false;

        if (lhs.index_variant().index() != rhs.index_variant().index())
            return lhs.index_variant().index() < rhs.index_variant().index();

        return lhs.apply(
            [&]<typename L>(const L& l)
            {
                return rhs.apply(
                    [&]<typename R>(const R& r)
                    {
                        if constexpr (std::is_same_v<std::remove_cvref_t<L>, std::remove_cvref_t<R>>)
                            return Less<std::remove_cvref_t<L>> {}(l, r);
                        return false;
                    });
            });
    }
};

}  // namespace ygg

#endif
