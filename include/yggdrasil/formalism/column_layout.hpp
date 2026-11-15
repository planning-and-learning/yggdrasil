/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_COLUMN_LAYOUT_HPP_
#define YGG_FORMALISM_COLUMN_LAYOUT_HPP_

#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/values.hpp"

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
    TypeId type;

    ColumnLayout() = default;
    ColumnLayout(urd::index<Column> label_, TypeId type_) : label(label_), type(type_) {}

    static constexpr auto cista_members() noexcept { return urd::Members<&ColumnLayout::label, &ColumnLayout::type> {}; }
    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"label", &ColumnLayout::label>, urd::field<"type", &ColumnLayout::type>> {}; }
    static constexpr auto identifying_members() noexcept { return urd::members_of(fields()); }
};
}  // namespace ygg

#endif
