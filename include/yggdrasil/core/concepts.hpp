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

#ifndef YGG_CORE_CONCEPTS_HPP_
#define YGG_CORE_CONCEPTS_HPP_

#include "yggdrasil/core/dependent_false.hpp"

#include <concepts>
#include <cstddef>
#include <ranges>
#include <type_traits>

namespace ygg
{

template<typename T>
struct EqualTo;

template<typename T>
struct Hash;

template<typename T>
struct Less;

/// @brief Check whether T has a function that returns members that aims to identify the class.
template<typename T>
concept Identifiable = requires(const T a) {
    { a.identifying_members() };
};

template<typename Range, typename Value>
concept InputRangeOf = std::ranges::input_range<Range> && std::same_as<std::ranges::range_value_t<Range>, Value>;

template<typename T>
concept TriviallyCopyable = std::is_trivially_copyable_v<T>;

template<typename T>
concept Enumeration = std::is_enum_v<T>;

template<typename T, typename U>
concept SameAsIgnoringCvref = std::same_as<std::remove_cvref_t<T>, U>;

template<typename T, typename U>
concept UnsignedIntegralSameAsIgnoringConst = std::unsigned_integral<std::remove_const_t<T>> && std::same_as<std::remove_const_t<T>, std::remove_const_t<U>>;

template<typename H, typename T>
concept HashFor = requires(const H& hash, const T& value) {
    { hash(value) } -> std::same_as<std::size_t>;
};

template<typename E, typename Lhs, typename Rhs = Lhs>
concept EqualToFor = requires(const E& equal_to, const Lhs& lhs, const Rhs& rhs) {
    { equal_to(lhs, rhs) } -> std::same_as<bool>;
};

template<typename C, typename Lhs, typename Rhs = Lhs>
concept LessFor = requires(const C& less, const Lhs& lhs, const Rhs& rhs) {
    { less(lhs, rhs) } -> std::same_as<bool>;
};

template<typename T>
concept Hashable = HashFor<Hash<std::remove_cvref_t<T>>, T>;

template<typename T>
concept EqualityComparableByEqualTo = EqualToFor<EqualTo<std::remove_cvref_t<T>>, T>;

template<typename T>
concept OrderedByLess = LessFor<Less<std::remove_cvref_t<T>>, T>;

}

#endif
