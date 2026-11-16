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
 * What the views of rows and of relations derive from a few primitives of their own, whatever they read: a stored
 * record (the record's urd::view_mixin) or a builder (RowBuilderView, RelationBuilderView). Each view gives only its
 * primitives; nothing here asks which kind of view it is.
 */

namespace ygg::detail
{
/// What a row's views V derive from its values, its columns' types and its relation's key (V's get_values(),
/// column_types() and relation_key()): its size, each column typed, each typed read checked against its type, and its
/// logical key.
template<typename F, typename V>
struct row_reads
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }
    /// A random-access range of them.
    [[nodiscard]] auto types() const { return self().column_types(); }
    /// Column i, std::out_of_range past the last.
    [[nodiscard]] std::size_t checked(std::size_t i) const
    {
        if (i >= size())
            throw std::out_of_range("Row: column index out of range.");
        return i;
    }
    template<typename T>
    static void check(TypeId type)
    {
        if (type != type_id<T>)
            throw std::invalid_argument("Row: the column's type is not the requested one.");
    }

public:
    [[nodiscard]] std::size_t size() const { return std::ranges::size(self().get_values()); }
    /// The canonical Value of column i; std::out_of_range past the last.
    [[nodiscard]] Value value(std::size_t i) const { return self().get_values()[checked(i)]; }
    /// The type of column i, its pool's; std::out_of_range past the last.
    [[nodiscard]] TypeId type(std::size_t i) const { return types()[checked(i)]; }
    /// Column i decoded as its type (an object column as its untagged object index).
    [[nodiscard]] ColumnValue get(std::size_t i) const { return decode(value(i), type(i)); }
    /// Column i as T; throws std::invalid_argument where the column's type is not T's (ygg::type_id).
    template<typename T>
    [[nodiscard]] T get(std::size_t i) const
    {
        check<T>(type(i));
        return decode_value<T>(value(i));
    }
    /// Columns first to last (exclusive) as T, a borrowed random-access range; throws std::invalid_argument unless each
    /// column's type is the one T reads (checked once, here).
    template<typename T>
    [[nodiscard]] auto get(std::size_t first, std::size_t last) const
    {
        const auto types = this->types();
        for (std::size_t i = first; i < last; ++i)
            check<T>(types[i]);
        decltype(auto) values = self().get_values();
        return urd::transform(std::ranges::subrange(std::ranges::next(values.begin(), static_cast<std::ptrdiff_t>(first)),
                                                    std::ranges::next(values.begin(), static_cast<std::ptrdiff_t>(last))),
                              &decode_value<T>);
    }
    /// Every column as T (see get<T>(first, last)).
    template<typename T>
    [[nodiscard]] auto get() const
    { return get<T>(0, size()); }
    /// The logical row across repositories: the relation index (a view's identity includes its context) and the
    /// canonical values.
    [[nodiscard]] auto get_key() const { return std::pair<urd::index<F>, decltype(self().get_values())>(self().relation_key(), self().get_values()); }
};
}  // namespace ygg::detail

namespace ygg
{
/// The shared read interface of borrowed and stored rows of family F. Identity is deliberately absent; a logical key
/// consists of the relation and the values.
template<typename V, typename F>
concept RowViewConcept = requires(const std::remove_reference_t<V>& view, std::size_t i) {
    view.context();
    { view.size() } -> std::same_as<std::size_t>;
    { view.value(i) } -> std::same_as<Value>;
    { view.type(i) } -> std::same_as<TypeId>;
    { view.get(i) } -> std::same_as<ColumnValue>;
    requires std::same_as<std::remove_cvref_t<decltype(view.get_key().first)>, urd::index<F>>;
    requires std::ranges::forward_range<const std::remove_reference_t<decltype(view.get_key().second)>>
                 && std::ranges::sized_range<const std::remove_reference_t<decltype(view.get_key().second)>>
                 && std::same_as<std::ranges::range_value_t<const std::remove_reference_t<decltype(view.get_key().second)>>, Value>;
};
}  // namespace ygg

namespace ygg::detail
{
/// What a relation's views V derive from its columns, its size and its rows (V's columns(), size() and get_rows()): its
/// arity, whether it is empty, a row by its position checked against its size, and whether it holds a row's values
/// (by V's contains(values)).
template<typename F, typename V>
struct relation_reads
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    /// The number of its columns.
    [[nodiscard]] std::size_t arity() const { return std::ranges::size(self().columns()); }
    [[nodiscard]] bool empty() const { return self().size() == 0; }
    /// The row at a position; std::out_of_range past the last.
    [[nodiscard]] auto row(std::size_t index) const
    {
        if (index >= self().size())
            throw std::out_of_range("Relation: row index out of range.");
        return self().get_rows()[index];
    }
    /// Whether it holds a row with the values of `row`, of any relation of F.
    template<RowViewConcept<F> R>
    [[nodiscard]] bool contains(const R& row) const
    { return self().contains(row.get_values()); }
};
}  // namespace ygg::detail

#endif
