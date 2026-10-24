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

#ifndef YGG_SERIALIZATION_CISTA_EQUAL_TO_HPP_
#define YGG_SERIALIZATION_CISTA_EQUAL_TO_HPP_

#include "yggdrasil/containers/array.hpp"
#include "yggdrasil/containers/optional.hpp"
#include "yggdrasil/containers/pair.hpp"
#include "yggdrasil/containers/variant.hpp"
#include "yggdrasil/containers/vector.hpp"
#include "yggdrasil/semantics/equal_to.hpp"

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
struct EqualTo<View<::cista::array<T, N>, C>>
{
    bool operator()(const View<::cista::array<T, N>, C>& lhs, const View<::cista::array<T, N>, C>& rhs) const
        requires EqualityComparableByEqualTo<std::ranges::range_value_t<const View<::cista::array<T, N>, C>>>
    { return equal_range(lhs, rhs); }
};

template<typename T1, typename T2>
struct EqualTo<::cista::pair<T1, T2>>
{
    constexpr bool operator()(const ::cista::pair<T1, T2>& lhs, const ::cista::pair<T1, T2>& rhs) const
        requires EqualityComparableByEqualTo<T1> && EqualityComparableByEqualTo<T2>
    { return EqualTo<std::remove_cvref_t<T1>> {}(lhs.first, rhs.first) && EqualTo<std::remove_cvref_t<T2>> {}(lhs.second, rhs.second); }
};

template<typename C, typename T1, typename T2>
struct EqualTo<View<::cista::pair<T1, T2>, C>>
{
    bool operator()(const View<::cista::pair<T1, T2>, C>& lhs, const View<::cista::pair<T1, T2>, C>& rhs) const
        requires requires {
            { lhs.get_first() } -> EqualityComparableByEqualTo;
            { lhs.get_second() } -> EqualityComparableByEqualTo;
        }
    { return EqualTo<ViewOrValue<T1, C>> {}(lhs.get_first(), rhs.get_first()) && EqualTo<ViewOrValue<T2, C>> {}(lhs.get_second(), rhs.get_second()); }
};

template<>
struct EqualTo<::cista::offset::string>
{
    bool operator()(const ::cista::offset::string& lhs, const ::cista::offset::string& rhs) const noexcept { return equal_range(lhs, rhs); }
};

template<typename T, template<typename> typename Ptr, bool IndexPointers, typename TemplateSizeType, class Allocator>
struct EqualTo<::cista::basic_vector<T, Ptr, IndexPointers, TemplateSizeType, Allocator>>
{
    bool operator()(const ::cista::basic_vector<T, Ptr, IndexPointers, TemplateSizeType, Allocator>& lhs,
                    const ::cista::basic_vector<T, Ptr, IndexPointers, TemplateSizeType, Allocator>& rhs) const
        requires EqualityComparableByEqualTo<T>
    { return equal_range(lhs, rhs); }
};

template<typename C, typename T, template<typename> typename Ptr, bool IndexPointers, typename TemplateSizeType, class Allocator>
struct EqualTo<View<::cista::basic_vector<T, Ptr, IndexPointers, TemplateSizeType, Allocator>, C>>
{
    bool operator()(const View<::cista::basic_vector<T, Ptr, IndexPointers, TemplateSizeType, Allocator>, C>& lhs,
                    const View<::cista::basic_vector<T, Ptr, IndexPointers, TemplateSizeType, Allocator>, C>& rhs) const
        requires EqualityComparableByEqualTo<
            std::ranges::range_value_t<const View<::cista::basic_vector<T, Ptr, IndexPointers, TemplateSizeType, Allocator>, C>>>
    { return equal_range(lhs, rhs); }
};

template<typename... Ts>
struct EqualTo<::cista::offset::variant<Ts...>>
{
    constexpr bool operator()(const ::cista::offset::variant<Ts...>& lhs, const ::cista::offset::variant<Ts...>& rhs) const
        requires(EqualityComparableByEqualTo<Ts> && ...)
    {
        if (lhs.valid() != rhs.valid())
            return false;

        if (!lhs.valid())
            return true;

        if (lhs.index() != rhs.index())
            return false;

        return lhs.apply(
            [&]<typename L>(const L& l)
            {
                return rhs.apply(
                    [&]<typename R>(const R& r)
                    {
                        if constexpr (std::is_same_v<std::remove_cvref_t<L>, std::remove_cvref_t<R>>)
                            return EqualTo<std::remove_cvref_t<L>> {}(l, r);
                        return false;
                    });
            });
    }
};

template<typename C, typename... Ts>
struct EqualTo<View<::cista::offset::variant<Ts...>, C>>
{
    bool operator()(const View<::cista::offset::variant<Ts...>, C>& lhs, const View<::cista::offset::variant<Ts...>, C>& rhs) const
        requires(EqualityComparableByEqualTo<ViewOrValue<Ts, C>> && ...)
    {
        if (lhs.valid() != rhs.valid())
            return false;

        if (!lhs.valid())
            return true;

        if (lhs.index_variant().index() != rhs.index_variant().index())
            return false;

        return lhs.apply(
            [&]<typename L>(const L& l)
            {
                return rhs.apply(
                    [&]<typename R>(const R& r)
                    {
                        if constexpr (std::is_same_v<std::remove_cvref_t<L>, std::remove_cvref_t<R>>)
                            return EqualTo<std::remove_cvref_t<L>> {}(l, r);
                        return false;
                    });
            });
    }
};

template<typename T>
struct EqualTo<::cista::optional<T>>
{
    bool operator()(const ::cista::optional<T>& lhs, const ::cista::optional<T>& rhs) const
        requires EqualityComparableByEqualTo<T>
    {
        if (!lhs.has_value() && !rhs.has_value())
            return true;

        if (lhs.has_value() != rhs.has_value())
            return false;

        return EqualTo<T> {}(*lhs, *rhs);
    }
};

template<typename C, typename T>
struct EqualTo<View<::cista::optional<T>, C>>
{
    bool operator()(const View<::cista::optional<T>, C>& lhs, const View<::cista::optional<T>, C>& rhs) const
        requires requires {
            { lhs.value() } -> EqualityComparableByEqualTo;
        }
    {
        if (!lhs.has_value() && !rhs.has_value())
            return true;

        if (lhs.has_value() != rhs.has_value())
            return false;

        return EqualTo<ViewOrValue<T, C>> {}(lhs.value(), rhs.value());
    }
};

}  // namespace ygg

#endif
