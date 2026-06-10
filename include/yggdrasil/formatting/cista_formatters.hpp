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

#ifndef YGG_COMMON_CISTA_FORMATTERS_HPP_
#define YGG_COMMON_CISTA_FORMATTERS_HPP_

#include "yggdrasil/containers/optional.hpp"
#include "yggdrasil/containers/variant.hpp"
#include "yggdrasil/containers/vector.hpp"
#include "yggdrasil/formatting/formatter.hpp"

#include <cista/containers/optional.h>
#include <cista/containers/string.h>
#include <cista/containers/variant.h>
#include <cista/containers/vector.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <string_view>
#include <type_traits>
#include <variant>

#if YGG_ENABLE_FMT_FORMATTERS
namespace fmt {

template <typename T, template <typename> typename Ptr, bool IndexPointers,
          typename TemplateSizeType, class Allocator, typename Char>
struct range_format_kind<
    ::cista::basic_vector<T, Ptr, IndexPointers, TemplateSizeType, Allocator>,
    Char, void> : std::false_type {};

template <typename Ptr, typename Char>
struct range_format_kind<::cista::basic_string<Ptr>, Char, void>
    : std::false_type {};

template <typename C, typename T, template <typename> typename Ptr,
          bool IndexPointers, typename TemplateSizeType, class Allocator,
          typename Char>
struct range_format_kind<
    ygg::View<::cista::basic_vector<T, Ptr, IndexPointers, TemplateSizeType,
                                    Allocator>,
              C>,
    Char, void> : std::false_type {};

template <typename Range, typename FormatContext>
auto format_sequence(const Range &value, FormatContext &ctx) {
  auto out = fmt::format_to(ctx.out(), "[");
  bool first = true;
  for (const auto &element : value) {
    if (!first)
      out = fmt::format_to(out, ", ");
    first = false;
    out = fmt::format_to(out, "{}", element);
  }
  return fmt::format_to(out, "]");
}

template <typename Ptr> struct formatter<::cista::basic_string<Ptr>, char> {
  constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }

  template <typename FormatContext>
  auto format(const ::cista::basic_string<Ptr> &value,
              FormatContext &ctx) const {
    return fmt::format_to(ctx.out(), "{}",
                          std::string_view(value.data(), value.size()));
  }
};

template <typename T> struct formatter<::cista::optional<T>, char> {
  constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }

  template <typename FormatContext>
  auto format(const ::cista::optional<T> &value, FormatContext &ctx) const {
    if (value.has_value())
      return fmt::format_to(ctx.out(), "{}", value.value());
    return fmt::format_to(ctx.out(), "<nullopt>");
  }
};

template <typename T, typename... Ts>
struct formatter<::cista::offset::variant<T, Ts...>, char> {
  constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }

  template <typename FormatContext>
  auto format(const ::cista::offset::variant<T, Ts...> &value,
              FormatContext &ctx) const {
    if (!value.valid())
      return fmt::format_to(ctx.out(), "<invalid>");

    return std::visit(
        [&](auto &&arg) { return fmt::format_to(ctx.out(), "{}", arg); },
        value);
  }
};

template <typename T, template <typename> typename Ptr, bool IndexPointers,
          typename TemplateSizeType, class Allocator>
struct formatter<
    ::cista::basic_vector<T, Ptr, IndexPointers, TemplateSizeType, Allocator>,
    char> {
  constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }

  template <typename FormatContext>
  auto format(const ::cista::basic_vector<T, Ptr, IndexPointers,
                                          TemplateSizeType, Allocator> &value,
              FormatContext &ctx) const {
    return format_sequence(value, ctx);
  }
};

template <typename C, typename T, template <typename> typename Ptr,
          bool IndexPointers, typename TemplateSizeType, class Allocator>
struct formatter<ygg::View<::cista::basic_vector<T, Ptr, IndexPointers,
                                                 TemplateSizeType, Allocator>,
                           C>,
                 char> {
  constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }

  template <typename FormatContext>
  auto
  format(const ygg::View<::cista::basic_vector<T, Ptr, IndexPointers,
                                               TemplateSizeType, Allocator>,
                         C> &value,
         FormatContext &ctx) const {
    return format_sequence(value, ctx);
  }
};

template <typename C, typename T>
struct formatter<ygg::View<::cista::optional<T>, C>, char> {
  constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }

  template <typename FormatContext>
  auto format(const ygg::View<::cista::optional<T>, C> &value,
              FormatContext &ctx) const {
    if (value.has_value())
      return fmt::format_to(ctx.out(), "{}", value.value());
    return fmt::format_to(ctx.out(), "<nullopt>");
  }
};

template <typename C, typename T, typename... Ts>
struct formatter<ygg::View<::cista::offset::variant<T, Ts...>, C>, char> {
  constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }

  template <typename FormatContext>
  auto format(const ygg::View<::cista::offset::variant<T, Ts...>, C> &value,
              FormatContext &ctx) const {
    if (!value.valid())
      return fmt::format_to(ctx.out(), "<invalid>");

    return visit(
        [&](auto &&arg) { return fmt::format_to(ctx.out(), "{}", arg); },
        value);
  }
};

} // namespace fmt
#endif

#endif
