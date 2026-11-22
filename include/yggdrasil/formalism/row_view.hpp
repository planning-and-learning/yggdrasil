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

#ifndef YGG_FORMALISM_ROW_VIEW_HPP_
#define YGG_FORMALISM_ROW_VIEW_HPP_

#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/relation_data.hpp"
#include "yggdrasil/formalism/row_data.hpp"
#include "yggdrasil/formalism/row_view_concepts.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <urd/elements/concepts.hpp>
#include <urd/elements/flat.hpp>
#include <urd/elements/view.hpp>
#include <utility>

namespace ygg::detail
{
/// Throws std::out_of_range where column i is past the last of a row of `size` columns.
inline void check_column(std::size_t i, std::size_t size)
{
    if (i >= size)
        throw std::out_of_range("Row: column index out of range.");
}
}  // namespace ygg::detail

namespace ygg
{
/// The values a row view V reads (its get_values()): a stored row's decoded from its pool, a held row's or a builder's
/// own.
template<typename V>
using row_values_t = std::remove_cvref_t<decltype(std::declval<const V&>().get_values())>;

/// Columns of a row view V read as T (get<T>(first, last)): a range of its values, each decoded. A class, so that it is
/// formed only where a member that returns it is used (V is incomplete where its mixin is).
template<typename V, typename T>
struct decoded_columns
{
    using type = urd::transformed_range<std::ranges::subrange<std::ranges::iterator_t<const row_values_t<V>>>, T (*)(Value) noexcept>;
};

template<typename V, typename T>
using decoded_columns_t = typename decoded_columns<V, T>::type;

/// Columns of a row view V read as the records of tag T they name (resolve<T>(first, last)), through its context.
template<typename V, typename T>
using resolved_columns_t = urd::resolved_range<decoded_columns_t<V, urd::index<T>>, urd::view_context_t<V>>;
}  // namespace ygg

/// A row of family F declares its views' accessors per form, below (a stored row's, an owned row's, a builder's).
template<typename F, typename V>
struct urd::view_mixin<ygg::Row<F>, V>
{
};

namespace ygg::detail
{
/// A row's untyped reads, for every row form (a stored row's, an owned row's, a builder's), which gives its values
/// (get_values()). The context and the builder, where borrowed, must outlive the view.
template<typename F, typename V>
struct row_value_reads
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    /// The canonical Value of a column; std::out_of_range past the last.
    [[nodiscard]] ygg::Value value(std::size_t column) const
    {
        decltype(auto) values = self().get_values();
        ygg::detail::check_column(column, std::ranges::size(values));
        return values[column];
    }

    static constexpr auto functions() noexcept { return urd::Functions<urd::function<"value", &row_value_reads::value, urd::arg<"column">>> {}; }
};

/// A row's typed reads, for a row whose relation is interned: a stored row (urd::index_view<Row<F>, C, urd::key<F,
/// Row<F>>>: view_mixin<PackedRow<F>, V>) and an owned row (view_mixin<data<Row<F>>, V>), not a builder (its relation a
/// child, of any form). They give its relation (get_relation(): the view of its interned pool key) and its values
/// (get_values()); the types of its columns are those its relation's record stores (get_column_types()). The context and
/// the record, where borrowed, must outlive the view and all ranges it gives.
template<typename F, typename V>
struct row_reads : row_value_reads<F, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    /// The type of a column, as its relation's record stores it; std::out_of_range past the last.
    [[nodiscard]] ygg::TypeId type(std::size_t column) const
    {
        const auto& types = self().get_relation().get_column_types();
        ygg::detail::check_column(column, std::ranges::size(types));
        return types[column];
    }

    /// A column decoded as its type (an object column as its untagged object index).
    [[nodiscard]] ygg::ColumnValue get(std::size_t column) const { return ygg::decode(self().value(column), type(column)); }

    /// A column as T; throws std::invalid_argument where the column's type is not T's (ygg::type_id).
    template<typename T>
    [[nodiscard]] T get(std::size_t column) const
    {
        if (type(column) != ygg::type_id<T>)
            throw std::invalid_argument("Row: the column's type is not the requested one.");
        return ygg::decode_value<T>(self().value(column));
    }

    /// Columns first to last (exclusive) as T, a borrowed random-access range; throws std::invalid_argument unless each
    /// column's type is the one T reads (checked once, here).
    template<typename T>
    [[nodiscard]] ygg::decoded_columns_t<V, T> get(std::size_t first, std::size_t last) const
    {
        const auto& values = self().get_values();
        const auto& types = self().get_relation().get_column_types();
        if (first < last)
            ygg::detail::check_column(last - 1, std::ranges::size(types));  // std::out_of_range past the last
        for (std::size_t column = first; column < last; ++column)
            if (types[column] != ygg::type_id<T>)
                throw std::invalid_argument("Row: the column's type is not the requested one.");
        return urd::transform(std::ranges::subrange(std::ranges::next(values.begin(), static_cast<std::ptrdiff_t>(first)),
                                                    std::ranges::next(values.begin(), static_cast<std::ptrdiff_t>(last))),
                              &ygg::decode_value<T>);
    }

    /// Every column as T (see get<T>(first, last)).
    template<typename T>
    [[nodiscard]] ygg::decoded_columns_t<V, T> get() const
    { return get<T>(0, std::ranges::size(self().get_values())); }

    /// The record of type T a column names, resolved through its context (a ground record's objects are its row's,
    /// `atom.get_row().resolve<Object>()`): where its context resolves indices of T (ResolvesColumns). Throws
    /// std::invalid_argument where the column's type is not urd::index<T> (get<T>).
    template<typename T>
        requires ygg::ResolvesColumns<V, T>
    [[nodiscard]] urd::index_view<T, urd::view_context_t<V>> resolve(std::size_t column) const
    { return urd::resolve(get<urd::index<T>>(column), self().context()); }

    /// Columns first to last (exclusive) as the records of type T they name (see resolve<T>(column)): a borrowed range.
    template<typename T>
        requires ygg::ResolvesColumns<V, T>
    [[nodiscard]] ygg::resolved_columns_t<V, T> resolve(std::size_t first, std::size_t last) const
    { return urd::resolve(get<urd::index<T>>(first, last), self().context()); }

    /// Every column as the record of type T it names (see resolve<T>(column)).
    template<typename T>
        requires ygg::ResolvesColumns<V, T>
    [[nodiscard]] ygg::resolved_columns_t<V, T> resolve() const
    { return urd::resolve(get<urd::index<T>>(), self().context()); }

    /// Its reads, and where its context resolves objects (ResolvesObjects) their resolutions: resolve_object(column),
    /// resolve_objects(first, last) and resolve_objects().
    static constexpr auto functions() noexcept
    {
        using Object = ygg::formalism::Object;
        constexpr auto reads =
            row_value_reads<F, V>::functions()
            + urd::Functions<urd::function<"type", &row_reads::type, urd::arg<"column">>,
                           urd::function<"get", static_cast<ygg::ColumnValue (row_reads::*)(std::size_t) const>(&row_reads::get), urd::arg<"column">>> {};
        if constexpr (ygg::ResolvesObjects<urd::view_context_t<V>>)
            return reads
                   + urd::Functions<
                       urd::function<"resolve_object",
                                     static_cast<urd::index_view<Object, urd::view_context_t<V>> (row_reads::*)(std::size_t) const>(
                                         &row_reads::template resolve<Object>),
                                     urd::arg<"column">>,
                       urd::function<"resolve_objects",
                                     static_cast<ygg::resolved_columns_t<V, Object> (row_reads::*)(std::size_t, std::size_t) const>(
                                         &row_reads::template resolve<Object>),
                                     urd::arg<"first">,
                                     urd::arg<"last">>,
                       urd::function<"resolve_objects",
                                     static_cast<ygg::resolved_columns_t<V, Object> (row_reads::*)() const>(&row_reads::template resolve<Object>)>> {};
        else
            return reads;
    }
};
}  // namespace ygg::detail

/// A stored row's view (urd::index_view<Row<F>, C, urd::key<F, Row<F>>>): its relation its key's first part, its values
/// the packed row its context hands out (PackedRow<F>), with the typed reads of every row form.
template<typename F, typename V>
struct urd::view_mixin<ygg::PackedRow<F>, V> : ygg::detail::row_reads<F, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    /// The view of its relation (its pool's key: its key's first part) through the context.
    [[nodiscard]] decltype(auto) get_relation() const { return urd::resolve(urd::get<F>(self().index()), self().context()); }

    /// Its canonical Values, read from its slot as they are needed.
    [[nodiscard]] ygg::PackedRow<F> get_values() const { return urd::record_at<ygg::Row<F>>(self().context(), self().index()); }
};

namespace ygg::detail
{
/// The members of a row held outside its pool, owned (data<Row<F>>) or built (builder<Row<F>, C>): its relation and its
/// values.
template<typename F, typename V>
struct held_row_members
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    /// The view of its relation (its pool's key) through the context.
    [[nodiscard]] decltype(auto) get_relation() const { return self().template member<&urd::record_of<V>::relation>(); }

    /// Its canonical Values.
    [[nodiscard]] decltype(auto) get_values() const { return self().template member<&urd::record_of<V>::values>(); }
};
}  // namespace ygg::detail

/// A row builder's view: its relation (a child: the view of the form it holds, interned or not) and its values, read
/// untyped only (value(column)); typed reads need an interned relation: an owned row's or a stored row's.
template<typename F, typename C, typename V>
struct urd::view_mixin<urd::builder<ygg::Row<F>, C>, V> : ygg::detail::held_row_members<F, V>, ygg::detail::row_value_reads<F, V>
{
};

/// An owned row's view (urd::data<Row<F>>, held outside its pool): its relation (an interned key) and its values, with
/// the typed reads.
template<typename F, typename V>
struct urd::view_mixin<urd::data<ygg::Row<F>>, V> : ygg::detail::held_row_members<F, V>, ygg::detail::row_reads<F, V>
{
};

#endif
