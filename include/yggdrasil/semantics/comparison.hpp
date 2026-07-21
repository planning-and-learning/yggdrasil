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

#ifndef YGG_SEMANTICS_COMPARISON_HPP_
#define YGG_SEMANTICS_COMPARISON_HPP_

#include "yggdrasil/semantics/comparators.hpp"
#include "yggdrasil/semantics/equal_to.hpp"

#include <concepts>

namespace ygg
{

template<Identifiable T>
constexpr bool operator==(const T& lhs, const T& rhs) noexcept
{
    return EqualTo<T> {}(lhs, rhs);
}

template<Identifiable T>
constexpr bool operator<(const T& lhs, const T& rhs) noexcept
{
    return Less<T> {}(lhs, rhs);
}

template<Identifiable T>
constexpr bool operator<=(const T& lhs, const T& rhs) noexcept
{
    return LessEqual<T> {}(lhs, rhs);
}

template<Identifiable T>
constexpr bool operator>(const T& lhs, const T& rhs) noexcept
{
    return Greater<T> {}(lhs, rhs);
}

template<Identifiable T>
constexpr bool operator>=(const T& lhs, const T& rhs) noexcept
{
    return GreaterEqual<T> {}(lhs, rhs);
}

namespace comparison
{

template<typename Derived>
struct Mixin
{
    friend constexpr bool operator==(const Derived& lhs, const Derived& rhs) noexcept
        requires Identifiable<Derived>
    {
        return ygg::operator==(lhs, rhs);
    }

    friend constexpr bool operator<(const Derived& lhs, const Derived& rhs) noexcept
        requires Identifiable<Derived>
    {
        return ygg::operator<(lhs, rhs);
    }

    friend constexpr bool operator<=(const Derived& lhs, const Derived& rhs) noexcept
        requires Identifiable<Derived>
    {
        return ygg::operator<=(lhs, rhs);
    }

    friend constexpr bool operator>(const Derived& lhs, const Derived& rhs) noexcept
        requires Identifiable<Derived>
    {
        return ygg::operator>(lhs, rhs);
    }

    friend constexpr bool operator>=(const Derived& lhs, const Derived& rhs) noexcept
        requires Identifiable<Derived>
    {
        return ygg::operator>=(lhs, rhs);
    }
};

}  // namespace comparison

template<typename T>
concept Comparable = requires(const T& i, const T& j) {
    { i == j } -> std::same_as<bool>;
    { i != j } -> std::same_as<bool>;
    { i <= j } -> std::same_as<bool>;
    { i < j } -> std::same_as<bool>;
    { i >= j } -> std::same_as<bool>;
    { i > j } -> std::same_as<bool>;
};

}  // namespace ygg

#endif
