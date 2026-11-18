/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_COLUMN_LAYOUT_HPP_
#define YGG_FORMALISM_COLUMN_LAYOUT_HPP_

#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/values.hpp"

#include <algorithm>
#include <concepts>
#include <span>
#include <stdexcept>
#include <variant>
#include <string_view>
#include <urd/elements/flat.hpp>
#include <urd/elements/less.hpp>

namespace ygg
{
/// A column: its label and type. Where its values lie in a row, the widths of its relation's pool say.
struct ColumnLayout : urd::compared_by_identity<ColumnLayout>
{
    static constexpr std::string_view name = "ColumnLayout";

    urd::index<Column> label;
    TypeId type {};

    ColumnLayout() = default;

    ColumnLayout(urd::index<Column> label_, TypeId type_) : label(label_), type(type_) {}

    static constexpr auto cista_members() noexcept { return urd::Members<&ColumnLayout::label, &ColumnLayout::type> {}; }

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"label", &ColumnLayout::label>, urd::field<"type", &ColumnLayout::type>> {}; }

    static constexpr auto identifying_members() noexcept { return urd::members_of(fields()); }
};

/// Whether a column before `column` has its label.
[[nodiscard]] inline bool repeats_label(std::span<const ColumnLayout> columns, std::span<const ColumnLayout>::iterator column)
{ return std::ranges::find(columns.begin(), column, column->label, &ColumnLayout::label) != column; }

/// Throws std::invalid_argument where a column's type is invalid or two columns share a label.
inline void validate_columns(std::span<const ColumnLayout> columns)
{
    for (auto it = columns.begin(); it != columns.end(); ++it)
    {
        if (static_cast<std::size_t>(it->type) >= std::variant_size_v<ColumnValue>)
            throw std::invalid_argument("Columns: invalid column type.");
        if (repeats_label(columns, it))
            throw std::invalid_argument("Columns: duplicate column label.");
    }
}
}  // namespace ygg

/// A column label is no element of a context, but a tag: read as itself, written only from itself.
template<>
struct urd::resolution<urd::index<ygg::Column>>
{
    static constexpr bool identity = true;

    template<typename C>
    static const urd::index<ygg::Column>& of(const urd::index<ygg::Column>& label, const C& /*context*/) noexcept
    { return label; }  // NOLINT(bugprone-return-const-ref-from-parameter)

    template<std::same_as<urd::index<ygg::Column>> R>
    static urd::index<ygg::Column> stored(const R& label) noexcept
    { return label; }
};

#endif
