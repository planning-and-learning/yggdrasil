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

#ifndef YGG_IDS_INDEX_MIXINS_HPP_
#define YGG_IDS_INDEX_MIXINS_HPP_

#include "yggdrasil/core/concepts.hpp"
#include "yggdrasil/core/config.hpp"

#include <limits>
#include <tuple>

namespace ygg
{

template<typename Derived>
struct IndexMixin
{
    using value_type = uint_t;

    value_type value {};

    static constexpr value_type MAX = std::numeric_limits<value_type>::max();

    constexpr IndexMixin() noexcept : value(MAX) {}
    constexpr explicit IndexMixin(value_type value) noexcept : value(value) {}

    static constexpr Derived max() noexcept { return Derived(MAX); }

    constexpr bool is_max() const noexcept { return value == MAX; }

    // ----------------------------------------------------
    // Comparisons
    // ----------------------------------------------------

    friend constexpr bool operator==(const IndexMixin& lhs, const IndexMixin& rhs) noexcept { return lhs.value == rhs.value; }
    friend constexpr bool operator!=(const IndexMixin& lhs, const IndexMixin& rhs) noexcept { return !(lhs == rhs); }
    friend constexpr bool operator<=(const IndexMixin& lhs, const IndexMixin& rhs) noexcept { return lhs.value <= rhs.value; }
    friend constexpr bool operator<(const IndexMixin& lhs, const IndexMixin& rhs) noexcept { return lhs.value < rhs.value; }
    friend constexpr bool operator>=(const IndexMixin& lhs, const IndexMixin& rhs) noexcept { return lhs.value >= rhs.value; }
    friend constexpr bool operator>(const IndexMixin& lhs, const IndexMixin& rhs) noexcept { return lhs.value > rhs.value; }

    explicit constexpr operator value_type() const noexcept { return value; }

    constexpr value_type get_value() const noexcept { return value; }

    auto cista_members() const noexcept { return std::tie(value); }
    auto identifying_members() const noexcept { return std::tie(value); }
};

template<typename T>
concept IndexConcept = requires(const T& i, const T& j, uint_t v) {
    { T() };
    { T(v) };
    { i.get_value() } -> std::same_as<uint_t>;
    { uint_t(i) } -> std::same_as<uint_t>;
    { T::max() } -> std::same_as<T>;
    { i.is_max() } -> std::same_as<bool>;
    { i == j } -> std::same_as<bool>;
    { i != j } -> std::same_as<bool>;
    { i <= j } -> std::same_as<bool>;
    { i < j } -> std::same_as<bool>;
    { i >= j } -> std::same_as<bool>;
    { i > j } -> std::same_as<bool>;
};

}  // namespace ygg

#endif
