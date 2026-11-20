/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_RELATION_VIEW_CONCEPTS_HPP_
#define YGG_FORMALISM_RELATION_VIEW_CONCEPTS_HPP_

#include "yggdrasil/formalism/column_layout.hpp"
#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/values.hpp"

#include <concepts>
#include <cstddef>
#include <optional>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>

namespace ygg
{
/// Whether V is the view of a relation of family F (an interned one, a builder's: its tag is Relation<F>): its columns,
/// its rows in tuple order (a row as a row view, or a builder's as the variant of the views of its forms), membership
/// and a row's place by values. Read through const, however V is spelled (a reference to a mutable view too).
template<typename V, typename F>
concept RelationViewConcept = std::same_as<typename std::remove_cvref_t<V>::value_type, Relation<F>>
                              && requires(const std::remove_reference_t<V>& view, std::size_t i, std::span<const Value> values) {
                                     view.context();
                                     { view.columns() } -> std::same_as<std::span<const urd::data<ColumnLayout>>>;
                                     { view.arity() } -> std::same_as<std::size_t>;
                                     { view.get_rows() } -> std::ranges::sized_range;
                                     { view.get_rows() } -> std::ranges::random_access_range;
                                     view.row(i);
                                     { view.locate(values) } -> std::same_as<std::pair<std::size_t, bool>>;
                                     { view.find(values) } -> std::same_as<std::optional<std::size_t>>;
                                     { view.contains(values) } -> std::same_as<bool>;
                                 };
}  // namespace ygg

#endif
