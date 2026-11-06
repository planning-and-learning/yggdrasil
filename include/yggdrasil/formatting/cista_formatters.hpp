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

#ifndef YGG_FORMATTING_CISTA_FORMATTERS_HPP_
#define YGG_FORMATTING_CISTA_FORMATTERS_HPP_

#include "yggdrasil/core/config.hpp"
#include "yggdrasil/formatting/formatter.hpp"

#include <cista/containers/optional.h>
#include <cista/containers/pair.h>
#include <cista/containers/string.h>
#include <cista/containers/variant.h>
#include <cista/containers/vector.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <string_view>
#include <type_traits>
#include <variant>

#if YGG_ENABLE_FMT_FORMATTERS
namespace fmt
{

// cista defines this exact specialization itself when CISTA_FMT is set
// (cista/containers/string.h), e.g. when linking the cista::cista target.
#if !defined(CISTA_FMT)
template<typename Ptr, typename Char>
struct range_format_kind<::cista::basic_string<Ptr>, Char, void> : std::false_type
{
};
#endif

template<typename Ptr>
struct formatter<::cista::basic_string<Ptr>, char>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const ::cista::basic_string<Ptr>& value, FormatContext& ctx) const
    { return fmt::format_to(ctx.out(), "{}", std::string_view(value.data(), value.size())); }
};

template<typename T>
struct formatter<::cista::optional<T>, char>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const ::cista::optional<T>& value, FormatContext& ctx) const
    {
        if (value.has_value())
            return fmt::format_to(ctx.out(), "{}", value.value());
        return fmt::format_to(ctx.out(), "<nullopt>");
    }
};

template<typename T, typename... Ts>
struct formatter<::cista::offset::variant<T, Ts...>, char>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const ::cista::offset::variant<T, Ts...>& value, FormatContext& ctx) const
    {
        if (!value.valid())
            return fmt::format_to(ctx.out(), "<invalid>");

        return std::visit([&](auto&& arg) { return fmt::format_to(ctx.out(), "{}", arg); }, value);
    }
};

// Variants read through a context (urd::resolve) print as the stored ones: the alternative held.
template<typename... Ts>
struct formatter<std::variant<Ts...>, char>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const std::variant<Ts...>& value, FormatContext& ctx) const
    { return std::visit([&](const auto& arg) { return fmt::format_to(ctx.out(), "{}", arg); }, value); }
};

}  // namespace fmt
#endif

#endif
