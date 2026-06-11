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

#ifndef YGG_COMMON_CISTA_HASH_HPP_
#define YGG_COMMON_CISTA_HASH_HPP_

#include "yggdrasil/containers/optional.hpp"
#include "yggdrasil/containers/variant.hpp"
#include "yggdrasil/containers/vector.hpp"
#include "yggdrasil/semantics/hash.hpp"

#include <cista/containers/optional.h>
#include <cista/containers/string.h>
#include <cista/containers/variant.h>
#include <cista/containers/vector.h>

#include <type_traits>

namespace ygg {

template <> struct Hash<::cista::offset::string> {
  using Type = ::cista::offset::string;

  size_t operator()(const Type &el) const noexcept { return ygg::hash_range(el); }
};

template <typename T, template <typename> typename Ptr, bool IndexPointers,
          typename TemplateSizeType, class Allocator>
struct Hash<
    ::cista::basic_vector<T, Ptr, IndexPointers, TemplateSizeType, Allocator>> {
  using Type =
      ::cista::basic_vector<T, Ptr, IndexPointers, TemplateSizeType, Allocator>;

  size_t operator()(const Type &el) const noexcept { return ygg::hash_range(el); }
};

template <typename C, typename T, template <typename> typename Ptr,
          bool IndexPointers, typename TemplateSizeType, class Allocator>
struct Hash<View<
    ::cista::basic_vector<T, Ptr, IndexPointers, TemplateSizeType, Allocator>,
    C>> {
  using Type = View<
      ::cista::basic_vector<T, Ptr, IndexPointers, TemplateSizeType, Allocator>,
      C>;

  size_t operator()(const Type &el) const noexcept { return ygg::hash_range(el); }
};

template <typename... Ts> struct Hash<::cista::offset::variant<Ts...>> {
  using Type = ::cista::offset::variant<Ts...>;

  size_t operator()(const Type &el) const noexcept {
    size_t seed = el.index();
    if (el.valid())
      el.apply([&seed](auto &&arg) { ygg::hash_combine(seed, arg); });
    return seed;
  }
};

template <typename C, typename... Ts>
struct Hash<View<::cista::offset::variant<Ts...>, C>> {
  using Type = View<::cista::offset::variant<Ts...>, C>;

  size_t operator()(const Type &el) const noexcept {
    size_t seed = el.index_variant().index();
    if (el.valid())
      el.apply([&seed](auto &&arg) { ygg::hash_combine(seed, arg); });
    return seed;
  }
};

template <typename T> struct Hash<::cista::optional<T>> {
  using Type = ::cista::optional<T>;

  size_t operator()(const Type &el) const noexcept {
    size_t seed = el.has_value() ? 1 : 0;
    if (el.has_value())
      ygg::hash_combine(seed, *el);
    return seed;
  }
};

template <typename C, typename T> struct Hash<View<::cista::optional<T>, C>> {
  using Type = View<::cista::optional<T>, C>;

  size_t operator()(const Type &el) const noexcept {
    size_t seed = el.has_value() ? 1 : 0;
    if (el.has_value())
      ygg::hash_combine(seed, el.value());
    return seed;
  }
};

} // namespace ygg

#endif
