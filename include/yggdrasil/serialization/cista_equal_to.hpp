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

#ifndef YGG_COMMON_CISTA_EQUAL_TO_HPP_
#define YGG_COMMON_CISTA_EQUAL_TO_HPP_

#include "yggdrasil/containers/optional.hpp"
#include "yggdrasil/containers/variant.hpp"
#include "yggdrasil/containers/vector.hpp"
#include "yggdrasil/semantics/equal_to.hpp"

#include <cista/containers/optional.h>
#include <cista/containers/string.h>
#include <cista/containers/variant.h>
#include <cista/containers/vector.h>
#include <type_traits>

namespace ygg
{

template<>
struct EqualTo<::cista::offset::string>
{
    using Type = ::cista::offset::string;

    bool operator()(const Type& lhs, const Type& rhs) const noexcept { return equal_range(lhs, rhs); }
};

template<typename T, template<typename> typename Ptr, bool IndexPointers, typename TemplateSizeType, class Allocator>
struct EqualTo<::cista::basic_vector<T, Ptr, IndexPointers, TemplateSizeType, Allocator>>
{
    using Type = ::cista::basic_vector<T, Ptr, IndexPointers, TemplateSizeType, Allocator>;

    bool operator()(const Type& lhs, const Type& rhs) const noexcept { return equal_range(lhs, rhs); }
};

template<typename C, typename T, template<typename> typename Ptr, bool IndexPointers, typename TemplateSizeType, class Allocator>
struct EqualTo<View<::cista::basic_vector<T, Ptr, IndexPointers, TemplateSizeType, Allocator>, C>>
{
    using Type = View<::cista::basic_vector<T, Ptr, IndexPointers, TemplateSizeType, Allocator>, C>;

    bool operator()(const Type& lhs, const Type& rhs) const noexcept { return equal_range(lhs, rhs); }
};

template<typename... Ts>
struct EqualTo<::cista::offset::variant<Ts...>>
{
    using Type = ::cista::offset::variant<Ts...>;

    bool operator()(const Type& lhs, const Type& rhs) const noexcept
    {
        if (lhs.valid() != rhs.valid())
            return false;

        if (!lhs.valid())
            return true;

        if (lhs.index() != rhs.index())
            return false;

        return lhs.apply(
            [&](auto&& l)
            {
                return rhs.apply(
                    [&](auto&& r)
                    {
                        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(l)>, std::remove_cvref_t<decltype(r)>>)
                            return EqualTo<std::remove_cvref_t<decltype(l)>> {}(l, r);
                        return false;
                    });
            });
    }
};

template<typename C, typename... Ts>
struct EqualTo<View<::cista::offset::variant<Ts...>, C>>
{
    using Type = View<::cista::offset::variant<Ts...>, C>;

    bool operator()(const Type& lhs, const Type& rhs) const noexcept
    {
        if (lhs.valid() != rhs.valid())
            return false;

        if (!lhs.valid())
            return true;

        if (lhs.index_variant().index() != rhs.index_variant().index())
            return false;

        return lhs.apply(
            [&](auto&& l)
            {
                return rhs.apply(
                    [&](auto&& r)
                    {
                        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(l)>, std::remove_cvref_t<decltype(r)>>)
                            return EqualTo<std::remove_cvref_t<decltype(l)>> {}(l, r);
                        return false;
                    });
            });
    }
};

template<typename T>
struct EqualTo<::cista::optional<T>>
{
    using Type = ::cista::optional<T>;

    bool operator()(const Type& lhs, const Type& rhs) const noexcept
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
    using Type = View<::cista::optional<T>, C>;

    bool operator()(const Type& lhs, const Type& rhs) const noexcept
    {
        if (!lhs.has_value() && !rhs.has_value())
            return true;

        if (lhs.has_value() != rhs.has_value())
            return false;

        return EqualTo<std::remove_cvref_t<decltype(lhs.value())>> {}(lhs.value(), rhs.value());
    }
};

}  // namespace ygg

#endif
