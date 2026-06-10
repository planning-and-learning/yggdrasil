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

#ifndef YGG_COMMON_EQUAL_TO_HPP_
#define YGG_COMMON_EQUAL_TO_HPP_

#include "yggdrasil/core/concepts.hpp"

#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <gtl/btree.hpp>

namespace ygg {

template <std::ranges::input_range LhsRange, std::ranges::input_range RhsRange>
inline bool equal_range(LhsRange &&lhs, RhsRange &&rhs) noexcept;

/// @brief `EqualTo` is our custom equality comparator, like std::equal_to.
///
/// Forwards to std::equal_to by default.
/// Specializations can be injected into the namespace.
template <typename T = void> struct EqualTo {
  bool operator()(const T &lhs, const T &rhs) const noexcept {
    return std::equal_to<T>{}(lhs, rhs);
  }
};

template <> struct EqualTo<void> {
  using is_transparent = void;

  template <typename T, typename U>
  bool operator()(const T &lhs, const U &rhs) const noexcept {
    return EqualTo<std::remove_cvref_t<T>>{}(lhs, rhs);
  }
};

template <std::floating_point T> struct EqualTo<T> {
  bool operator()(const T &lhs, const T &rhs) const noexcept {
    if (std::isnan(lhs) || std::isnan(rhs))
      return std::isnan(lhs) && std::isnan(rhs);

    return std::equal_to<T>{}(lhs, rhs);
  }
};

template <typename T, size_t N> struct EqualTo<std::array<T, N>> {
  bool operator()(const std::array<T, N> &lhs,
                  const std::array<T, N> &rhs) const noexcept {
    return equal_range(lhs, rhs);
  }
};

template <typename T> struct EqualTo<std::reference_wrapper<T>> {
  bool operator()(const std::reference_wrapper<T> &lhs,
                  const std::reference_wrapper<T> &rhs) const noexcept {
    return EqualTo<std::remove_cvref_t<T>>{}(lhs.get(), rhs.get());
  }
};

template <typename Key, typename Compare, typename Allocator>
struct EqualTo<std::set<Key, Compare, Allocator>> {
  bool operator()(const std::set<Key, Compare, Allocator> &lhs,
                  const std::set<Key, Compare, Allocator> &rhs) const noexcept {
    return equal_range(lhs, rhs);
  }
};

template <typename Key, typename T, typename Compare, typename Allocator>
struct EqualTo<std::map<Key, T, Compare, Allocator>> {
  bool
  operator()(const std::map<Key, T, Compare, Allocator> &lhs,
             const std::map<Key, T, Compare, Allocator> &rhs) const noexcept {
    return equal_range(lhs, rhs);
  }
};

template <typename Key, typename Compare, typename Allocator>
struct EqualTo<gtl::btree_set<Key, Compare, Allocator>> {
  bool operator()(
      const gtl::btree_set<Key, Compare, Allocator> &lhs,
      const gtl::btree_set<Key, Compare, Allocator> &rhs) const noexcept {
    return equal_range(lhs, rhs);
  }
};

template <typename Key, typename T, typename Compare, typename Allocator>
struct EqualTo<gtl::btree_map<Key, T, Compare, Allocator>> {
  bool operator()(
      const gtl::btree_map<Key, T, Compare, Allocator> &lhs,
      const gtl::btree_map<Key, T, Compare, Allocator> &rhs) const noexcept {
    return equal_range(lhs, rhs);
  }
};

template <typename T, typename Allocator>
struct EqualTo<std::vector<T, Allocator>> {
  bool operator()(const std::vector<T, Allocator> &lhs,
                  const std::vector<T, Allocator> &rhs) const noexcept {
    return equal_range(lhs, rhs);
  }
};

template <typename T1, typename T2> struct EqualTo<std::pair<T1, T2>> {
  bool operator()(const std::pair<T1, T2> &lhs,
                  const std::pair<T1, T2> &rhs) const noexcept {
    return EqualTo<std::remove_cvref_t<T1>>()(lhs.first, rhs.first) &&
           EqualTo<std::remove_cvref_t<T2>>{}(lhs.second, rhs.second);
  }
};

template <typename... Ts> struct EqualTo<std::tuple<Ts...>> {
  bool operator()(const std::tuple<Ts...> &lhs,
                  const std::tuple<Ts...> &rhs) const noexcept {
    return std::apply(
        [&rhs](const Ts &...lhs_args) {
          return std::apply(
              [&lhs_args...](const Ts &...rhs_args) {
                return (
                    EqualTo<std::remove_cvref_t<Ts>>{}(lhs_args, rhs_args) &&
                    ...);
              },
              rhs);
        },
        lhs);
  }
};

template <typename... Ts> struct EqualTo<std::variant<Ts...>> {
  bool operator()(const std::variant<Ts...> &lhs,
                  const std::variant<Ts...> &rhs) const noexcept {
    if (lhs.index() != rhs.index())
      return false;

    return std::visit(
        [](const auto &l, const auto &r) {
          if constexpr (std::is_same_v<std::remove_cvref_t<decltype(l)>,
                                       std::remove_cvref_t<decltype(r)>>)
            return EqualTo<std::remove_cvref_t<decltype(l)>>{}(l, r);
          return false;
        },
        lhs, rhs);
  }
};

template <typename T> struct EqualTo<std::optional<T>> {
  bool operator()(const std::optional<T> &lhs,
                  const std::optional<T> &rhs) const noexcept {
    // Check for presence of values
    if (lhs.has_value() != rhs.has_value())
      return false;

    // If both are empty, they're equal
    if (!lhs.has_value() && !rhs.has_value())
      return true;

    // Compare the contained values using EqualTo
    return EqualTo<std::remove_cvref_t<T>>{}(lhs.value(), rhs.value());
  }
};

template <typename T, std::size_t Extent> struct EqualTo<std::span<T, Extent>> {
  bool operator()(const std::span<T, Extent> &lhs,
                  const std::span<T, Extent> &rhs) const noexcept {
    return equal_range(lhs, rhs);
  }
};

template <Identifiable T> struct EqualTo<T> {
  using is_transparent = void;

  using MembersTupleType = decltype(std::declval<T>().identifying_members());

  bool operator()(const T &lhs, const T &rhs) const noexcept {
    return EqualTo<std::remove_cvref_t<MembersTupleType>>{}(
        lhs.identifying_members(), rhs.identifying_members());
  }

  template <SameAsIgnoringCvref<MembersTupleType> U>
  bool operator()(const T &a, const U &v) const noexcept {
    return EqualTo<std::remove_cvref_t<MembersTupleType>>{}(
        a.identifying_members(), v);
  }

  template <SameAsIgnoringCvref<MembersTupleType> U>
  bool operator()(const U &v, const T &b) const noexcept {
    return EqualTo<std::remove_cvref_t<MembersTupleType>>{}(
        v, b.identifying_members());
  }

  template <SameAsIgnoringCvref<MembersTupleType> U,
            SameAsIgnoringCvref<MembersTupleType> V>
  bool operator()(const U &u, const V &v) const noexcept {
    return EqualTo<std::remove_cvref_t<MembersTupleType>>{}(u, v);
  }
};

template <std::ranges::input_range LhsRange, std::ranges::input_range RhsRange>
inline bool equal_range(LhsRange &&lhs, RhsRange &&rhs) noexcept {
  if constexpr (std::ranges::sized_range<LhsRange> &&
                std::ranges::sized_range<RhsRange>) {
    if (std::ranges::size(lhs) != std::ranges::size(rhs))
      return false;
  }

  auto lhs_it = std::ranges::begin(lhs);
  auto rhs_it = std::ranges::begin(rhs);
  const auto lhs_end = std::ranges::end(lhs);
  const auto rhs_end = std::ranges::end(rhs);

  for (; lhs_it != lhs_end && rhs_it != rhs_end; ++lhs_it, ++rhs_it) {
    using LhsValue = std::remove_cvref_t<decltype(*lhs_it)>;
    using RhsValue = std::remove_cvref_t<decltype(*rhs_it)>;
    if constexpr (std::same_as<LhsValue, RhsValue>) {
      if (!EqualTo<LhsValue>{}(*lhs_it, *rhs_it))
        return false;
    } else {
      return false;
    }
  }

  return lhs_it == lhs_end && rhs_it == rhs_end;
}

} // namespace ygg

#endif
