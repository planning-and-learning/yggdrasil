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

#ifndef YGG_COMMON_FORMATTER_HPP_
#define YGG_COMMON_FORMATTER_HPP_

#include "ygg/core/config.hpp"

#include <fmt/format.h>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <variant>
#include <vector>

namespace ygg
{

template<typename T>
struct Index;

template<typename T, typename C>
struct View;

template<typename T>
std::string to_string(const T& element)
{
    return fmt::format("{}", element);
}

}  // namespace ygg

#if YGG_ENABLE_FMT_FORMATTERS
namespace fmt
{

template<typename T>
struct formatter<ygg::Index<T>, char>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const ygg::Index<T>& value, FormatContext& ctx) const
    {
        return fmt::format_to(ctx.out(), "{}", ygg::uint_t(value));
    }
};

template<typename T>
struct formatter<std::optional<T>, char>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const std::optional<T>& value, FormatContext& ctx) const
    {
        if (value.has_value())
            return fmt::format_to(ctx.out(), "{}", value.value());
        return fmt::format_to(ctx.out(), "<nullopt>");
    }
};

template<typename T>
struct formatter<std::shared_ptr<T>, char>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const std::shared_ptr<T>& value, FormatContext& ctx) const
    {
        if (value)
            return fmt::format_to(ctx.out(), "{}", *value);
        return fmt::format_to(ctx.out(), "<nullptr>");
    }
};

template<typename T, typename Deleter>
struct formatter<std::unique_ptr<T, Deleter>, char>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const std::unique_ptr<T, Deleter>& value, FormatContext& ctx) const
    {
        if (value)
            return fmt::format_to(ctx.out(), "{}", *value);
        return fmt::format_to(ctx.out(), "<nullptr>");
    }
};

template<>
struct formatter<std::monostate, char>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const std::monostate&, FormatContext& ctx) const
    {
        return fmt::format_to(ctx.out(), "monostate");
    }
};

}  // namespace fmt
#endif

namespace ygg
{

template<std::ranges::input_range Range>
std::vector<std::string> to_strings(const Range& range)
{
    auto result = std::vector<std::string> {};
    if constexpr (std::ranges::sized_range<Range>)
        result.reserve(std::ranges::size(range));
    for (const auto& element : range)
        result.push_back(to_string(element));
    return result;
}

}

#endif
