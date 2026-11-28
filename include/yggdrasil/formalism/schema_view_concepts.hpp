/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_SCHEMA_VIEW_CONCEPTS_HPP_
#define YGG_FORMALISM_SCHEMA_VIEW_CONCEPTS_HPP_

#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/values.hpp"

#include <concepts>
#include <ranges>
#include <type_traits>

namespace ygg
{
/// Whether V is the view of a Schema (interned, a builder's, a record's: its tag is Schema): its column types, as every
/// key of row pools gives them (column_types()). Read through const, however V is spelled.
template<typename V>
concept SchemaViewConcept = std::same_as<typename std::remove_cvref_t<V>::value_type, Schema> && requires(const std::remove_reference_t<V>& view) {
    view.context();
    { view.column_types() } -> std::ranges::random_access_range;
    { view.column_types() } -> std::ranges::random_access_range;
    requires std::same_as<std::ranges::range_value_t<decltype(view.column_types())>, TypeId>;
};
}  // namespace ygg

#endif
