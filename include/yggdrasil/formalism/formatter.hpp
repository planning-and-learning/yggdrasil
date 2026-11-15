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
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <string_view>
#include <urd/elements/view.hpp>
#include <urd/format.hpp>

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
#endif

#endif
