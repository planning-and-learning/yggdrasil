/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_FORMATTER_HPP_
#define YGG_FORMALISM_FORMATTER_HPP_

#include "yggdrasil/core/config.hpp"
#include "yggdrasil/formalism/relation_view.hpp"
#include "yggdrasil/formalism/row_view.hpp"

#if YGG_ENABLE_FMT_FORMATTERS
#include <cstddef>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
#include <urd/elements/view.hpp>
#include <urd/format.hpp>
#include <variant>

// What urd/format.hpp does not write of the free relations: a column and a row's key. Their records and views are
// written as urd writes a record.

namespace fmt
{

/// A column: label:type.
template<>
struct formatter<ygg::ColumnLayout, char>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const ygg::ColumnLayout& value, FormatContext& ctx) const
    { return fmt::format_to(ctx.out(), "{}:{}", value.label, value.type); }
};

/// A row's key: <pool,row>.
template<typename F>
struct formatter<ygg::RowIndex<F>, char>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const ygg::RowIndex<F>& value, FormatContext& ctx) const
    { return fmt::format_to(ctx.out(), "<{},{}>", value.relation, value.row); }
};

}  // namespace fmt

namespace ygg
{
/// A row's values, separated by spaces: each column as its type writes it, an object column as its object's view
/// (resolved through the row's context). The relation is not written: who knows its name
/// (a predicate's, an action's) writes it.
template<typename V>
std::string values_text(const V& row)
{
    auto result = std::string();
    for (std::size_t i = 0; i < row.size(); ++i)
    {
        if (i != 0)
            result.push_back(' ');
        std::visit(
            [&]<typename T>(T value)
            {
                if constexpr (std::same_as<T, urd::index<formalism::Object>>)
                    fmt::format_to(std::back_inserter(result), "{}", urd::resolve(value, row.context()));
                else
                    fmt::format_to(std::back_inserter(result), "{}", value);
            },
            row.get(i));
    }
    return result;
}
}  // namespace ygg
#endif

#endif
