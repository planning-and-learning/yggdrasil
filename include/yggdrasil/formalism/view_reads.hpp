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

#ifndef YGG_FORMALISM_VIEW_READS_HPP_
#define YGG_FORMALISM_VIEW_READS_HPP_

#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/values.hpp"

#include <concepts>
#include <cstddef>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <urd/elements/flat.hpp>
#include <utility>

/**
 * What the views of rows and of relations share beyond their mixins (urd::view_mixin<Row<F>, V>,
 * urd::view_mixin<Relation<F>, V>), which a builder's views inherit as well (RowBuilderView, RelationBuilderView): the
 * checks of a column read and a row's logical key.
 */

namespace ygg::detail
{
/// Column i of a row of `size` columns, std::out_of_range past the last.
[[nodiscard]] inline std::size_t checked_column(std::size_t i, std::size_t size)
{
    if (i >= size)
        throw std::out_of_range("Row: column index out of range.");
    return i;
}

/// Throws std::invalid_argument where a column of type `type` is not read as T (ygg::type_id).
template<typename T>
void check_column_type(TypeId type)
{
    if (type != type_id<T>)
        throw std::invalid_argument("Row: the column's type is not the requested one.");
}
}  // namespace ygg::detail

namespace ygg
{
/// The logical row of a row view across repositories: its relation's key (a view's identity includes its context)
/// and its canonical values.
template<typename R>
[[nodiscard]] auto key_of(const R& row)
{ return std::pair<std::remove_cvref_t<decltype(row.relation_key())>, decltype(row.get_values())>(row.relation_key(), row.get_values()); }

/// The shared read interface of borrowed and stored rows of family F. Identity is deliberately absent; a logical key
/// consists of the relation and the values (key_of).
template<typename V, typename F>
concept RowViewConcept = requires(const std::remove_reference_t<V>& view, std::size_t i) {
    view.context();
    { view.size() } -> std::same_as<std::size_t>;
    { view.value(i) } -> std::same_as<Value>;
    { view.type(i) } -> std::same_as<TypeId>;
    { view.get(i) } -> std::same_as<ColumnValue>;
    { view.relation_key() } -> std::same_as<urd::index<F>>;
    requires RowValues<std::remove_cvref_t<decltype(view.get_values())>>;
};
}  // namespace ygg

#endif
