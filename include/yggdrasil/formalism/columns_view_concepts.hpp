/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_COLUMNS_VIEW_CONCEPTS_HPP_
#define YGG_FORMALISM_COLUMNS_VIEW_CONCEPTS_HPP_

#include "yggdrasil/formalism/column_layout.hpp"
#include "yggdrasil/formalism/declarations.hpp"

#include <concepts>
#include <ranges>
#include <type_traits>

namespace ygg
{
/// Whether V is the view of a Columns (interned, a builder's, a record's: its tag is Columns): its columns, labeled and
/// typed. Read through const, however V is spelled.
template<typename V>
concept ColumnsViewConcept = std::same_as<typename std::remove_cvref_t<V>::value_type, Columns> && requires(const std::remove_reference_t<V>& view) {
    view.context();
    { view.get_values() } -> std::ranges::random_access_range;
    requires std::same_as<std::ranges::range_value_t<decltype(view.get_values())>, urd::data<ColumnLayout>>;
};
}  // namespace ygg

#endif
