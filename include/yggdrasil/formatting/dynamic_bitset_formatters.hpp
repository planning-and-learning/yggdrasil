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

#include <boost/dynamic_bitset.hpp>
#include "yggdrasil/core/config.hpp"

#include <concepts>
#include <cstddef>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <type_traits>
#include <urd/repr/containers/bitset_span.hpp>

#if YGG_ENABLE_FMT_FORMATTERS
namespace ygg::detail
{
/// Formats the positions of a bitset's set bits as {p, q, ...}.
struct SetBitsFormatter
{
    constexpr auto parse(fmt::format_parse_context& ctx) { return ctx.begin(); }

    template<typename Bits, typename FormatContext>
    auto format(const Bits& value, FormatContext& ctx) const
    {
        auto out = fmt::format_to(ctx.out(), "{{");
        auto first = true;
        for (auto pos = value.find_first(); pos != Bits::npos; pos = value.find_next(pos))
        {
            if (!first)
                out = fmt::format_to(out, ", ");
            first = false;
            out = fmt::format_to(out, "{}", pos);
        }
        return fmt::format_to(out, "}}");
    }
};
}  // namespace ygg::detail

namespace fmt
{
template<typename Block, typename Allocator, typename Char>
struct range_format_kind<boost::dynamic_bitset<Block, Allocator>, Char, void> : std::integral_constant<range_format, range_format::disabled>
{
};

template<typename Block, typename Allocator>
struct formatter<boost::dynamic_bitset<Block, Allocator>, char> : ygg::detail::SetBitsFormatter
{
};

template<std::unsigned_integral Block>
struct formatter<urd::repr::BitsetSpan<Block>, char> : ygg::detail::SetBitsFormatter
{
};
}
#endif

#endif
