/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_ROW_VIEW_CONCEPTS_HPP_
#define YGG_FORMALISM_ROW_VIEW_CONCEPTS_HPP_

#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/values.hpp"

#include <concepts>
#include <cstddef>
#include <type_traits>

namespace ygg
{
/// Whether V is the view of a row of family F (a stored row, one held outside the repository, a builder's: its tag is
/// Row<F>): its relation, its values, each column decoded, through its context. Read through const, however V is spelled
/// (a reference to a mutable view too).
template<typename V, typename F>
concept RowViewConcept = std::same_as<typename std::remove_cvref_t<V>::value_type, Row<F>> && requires(const std::remove_reference_t<V>& view, std::size_t i) {
    view.context();
    view.get_relation();
    { view.get_values() } -> RowValues;
    { view.value(i) } -> std::same_as<Value>;
    { view.type(i) } -> std::same_as<TypeId>;
    { view.get(i) } -> std::same_as<ColumnValue>;
};

/// Whether V is the view of a stored row of family F: its index() is the row's key.
template<typename V, typename F>
concept StoredRowView = requires(const V& view) {
    { view.index() } -> std::same_as<RowIndex<F>>;
};
}  // namespace ygg

#endif
