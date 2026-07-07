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

#ifndef YGG_FORMATTING_DYNAMIC_BITSET_FORMATTERS_HPP_
#define YGG_FORMATTING_DYNAMIC_BITSET_FORMATTERS_HPP_

#include "yggdrasil/containers/dynamic_bitset.hpp"
#include "yggdrasil/core/config.hpp"

#include <concepts>
#include <cstddef>
#include <fmt/format.h>

#if YGG_ENABLE_FMT_FORMATTERS
namespace fmt
{
template<typename Block, typename Allocator>
struct formatter<boost::dynamic_bitset<Block, Allocator>, char>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const boost::dynamic_bitset<Block, Allocator>& value, FormatContext& ctx) const
    {
        auto out = fmt::format_to(ctx.out(), "{{");
        auto first = true;
        for (auto pos = value.find_first(); pos != boost::dynamic_bitset<Block, Allocator>::npos; pos = value.find_next(pos))
        {
            if (!first)
                out = fmt::format_to(out, ", ");
            first = false;
            out = fmt::format_to(out, "{}", pos);
        }
        return fmt::format_to(out, "}}");
    }
};

template<std::unsigned_integral Block>
struct formatter<ygg::BitsetSpan<Block>, char>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const ygg::BitsetSpan<Block>& value, FormatContext& ctx) const
    {
        auto out = fmt::format_to(ctx.out(), "{{");
        size_t pos = value.find_first();
        bool first = true;
        while (pos != ygg::BitsetSpan<Block>::npos)
        {
            if (!first)
                out = fmt::format_to(out, ", ");
            first = false;
            out = fmt::format_to(out, "{}", pos);
            pos = value.find_next(pos);
        }
        return fmt::format_to(out, "}}");
    }
};
}
#endif

#endif
