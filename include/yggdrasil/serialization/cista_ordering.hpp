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
    using Type = View<::cista::array<T, N>, C>;

    bool operator()(const Type& lhs, const Type& rhs) const noexcept { return less_range(lhs, rhs); }
};

template<typename T1, typename T2>
struct Less<::cista::pair<T1, T2>>
{
    using Type = ::cista::pair<T1, T2>;

    bool operator()(const Type& lhs, const Type& rhs) const noexcept
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
    using Type = View<::cista::pair<T1, T2>, C>;

    bool operator()(const Type& lhs, const Type& rhs) const noexcept
    {
        using First = std::remove_cvref_t<decltype(lhs.get_first())>;
        if (Less<First> {}(lhs.get_first(), rhs.get_first()))
            return true;
        if (Less<First> {}(rhs.get_first(), lhs.get_first()))
            return false;
        return Less<std::remove_cvref_t<decltype(lhs.get_second())>> {}(lhs.get_second(), rhs.get_second());
    }
};

template<typename Ptr>
struct Less<::cista::basic_string<Ptr>>
{
    using Type = ::cista::basic_string<Ptr>;

    bool operator()(const Type& lhs, const Type& rhs) const noexcept { return less_range(lhs, rhs); }
};

template<typename T, template<typename> typename Ptr, bool IndexPointers, typename TemplateSizeType, class Allocator>
struct Less<::cista::basic_vector<T, Ptr, IndexPointers, TemplateSizeType, Allocator>>
{
    using Type = ::cista::basic_vector<T, Ptr, IndexPointers, TemplateSizeType, Allocator>;

    bool operator()(const Type& lhs, const Type& rhs) const noexcept { return less_range(lhs, rhs); }
};

template<typename C, typename T, template<typename> typename Ptr, bool IndexPointers, typename TemplateSizeType, class Allocator>
struct Less<View<::cista::basic_vector<T, Ptr, IndexPointers, TemplateSizeType, Allocator>, C>>
{
    using Type = View<::cista::basic_vector<T, Ptr, IndexPointers, TemplateSizeType, Allocator>, C>;

    bool operator()(const Type& lhs, const Type& rhs) const noexcept { return less_range(lhs, rhs); }
};

template<typename T>
struct Less<::cista::optional<T>>
{
    using Type = ::cista::optional<T>;

    bool operator()(const Type& lhs, const Type& rhs) const noexcept
    {
        if (lhs.has_value() != rhs.has_value())
            return !lhs.has_value();

        return lhs.has_value() ? Less<std::remove_cvref_t<T>> {}(*lhs, *rhs) : false;
    }
};

template<typename C, typename T>
struct Less<View<::cista::optional<T>, C>>
{
    using Type = View<::cista::optional<T>, C>;

    bool operator()(const Type& lhs, const Type& rhs) const noexcept
    {
        if (lhs.has_value() != rhs.has_value())
            return !lhs.has_value();

        return lhs.has_value() ? Less<std::remove_cvref_t<decltype(lhs.value())>> {}(lhs.value(), rhs.value()) : false;
    }
};

template<typename... Ts>
struct Less<::cista::offset::variant<Ts...>>
{
    using Type = ::cista::offset::variant<Ts...>;

    bool operator()(const Type& lhs, const Type& rhs) const noexcept
    {
        if (lhs.valid() != rhs.valid())
            return !lhs.valid();

        if (!lhs.valid())
            return false;

        if (lhs.index() != rhs.index())
            return lhs.index() < rhs.index();

        return lhs.apply(
            [&](auto&& l)
            {
                return rhs.apply(
                    [&](auto&& r)
                    {
                        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(l)>, std::remove_cvref_t<decltype(r)>>)
                            return Less<std::remove_cvref_t<decltype(l)>> {}(l, r);
                        return false;
                    });
            });
    }
};

template<typename C, typename... Ts>
struct Less<View<::cista::offset::variant<Ts...>, C>>
{
    using Type = View<::cista::offset::variant<Ts...>, C>;

    bool operator()(const Type& lhs, const Type& rhs) const noexcept
    {
        if (lhs.valid() != rhs.valid())
            return !lhs.valid();

        if (!lhs.valid())
            return false;

        if (lhs.index_variant().index() != rhs.index_variant().index())
            return lhs.index_variant().index() < rhs.index_variant().index();

        return lhs.apply(
            [&](auto&& l)
            {
                return rhs.apply(
                    [&](auto&& r)
                    {
                        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(l)>, std::remove_cvref_t<decltype(r)>>)
                            return Less<std::remove_cvref_t<decltype(l)>> {}(l, r);
                        return false;
                    });
            });
    }
};

}  // namespace ygg

#endif
