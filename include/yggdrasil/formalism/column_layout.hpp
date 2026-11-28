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
#include <string_view>
#include <type_traits>
#include <urd/elements/data.hpp>
#include <urd/elements/flat.hpp>
#include <urd/elements/less.hpp>
#include <variant>

namespace ygg
{
/// A column: its label and type (urd::data<ColumnLayout>, a value: no Store holds it). Where its values lie in a row,
/// the widths of its relation's pool say.
struct ColumnLayout
{
    static constexpr std::string_view name = "ColumnLayout";
};
}  // namespace ygg

template<>
struct urd::data<ygg::ColumnLayout>
{
    urd::index<ygg::Column> label;
    ygg::TypeId type {};

    data() = default;

    data(urd::index<ygg::Column> label_, ygg::TypeId type_) : label(label_), type(type_) {}

    static constexpr bool copyable() noexcept
    {
        static_assert(std::is_trivially_copyable_v<data>, "a copyable record is trivially copyable");
        return true;
    }

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"label", &data::label>, urd::field<"type", &data::type>> {}; }

    static constexpr auto cista_members() noexcept { return urd::cista_members_of<data>(); }
};

namespace ygg
{

/// Whether a column before `column` has its label.
[[nodiscard]] inline bool repeats_label(std::span<const urd::data<ColumnLayout>> columns, std::span<const urd::data<ColumnLayout>>::iterator column)
{ return std::ranges::find(columns.begin(), column, column->label, &urd::data<ColumnLayout>::label) != column; }

/// Throws std::invalid_argument where a column's type is invalid or two columns share a label.
inline void validate_columns(std::span<const urd::data<ColumnLayout>> columns)
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
