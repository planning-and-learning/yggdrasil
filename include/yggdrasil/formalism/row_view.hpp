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
#include <variant>

namespace ygg::detail
{
/// Throws std::out_of_range where column i is past the last of a row of `size` columns.
inline void check_column(std::size_t i, std::size_t size)
{
    if (i >= size)
        throw std::out_of_range("Row: column index out of range.");
}

/// `read` of a row's column types: its relation's (its pool's key's get_column_types(), a range, stored or computed), in
/// whatever form the row holds the relation (a view, or a variant of views: interned, built, a record not interned yet).
template<typename Relation, typename Read>
decltype(auto) with_column_types(const Relation& relation, Read read)
{
    return urd::visit_view(relation, [&read](const auto& each) -> decltype(auto) { return read(each.get_column_types()); });
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

/// A row's accessors, for a stored row (urd::index_view<Row<F>, C, RowIndex<F>>, whose context reads each field of
/// data<Row<F>> by its key: urd::ReadsFieldsByKey) and a row held outside the repository (urd::borrowed_view of its record) alike: its
/// relation, its values and their types. The context and the record, where borrowed, must outlive the view and all
/// ranges it gives.
template<typename F, typename V>
struct urd::view_mixin<ygg::Row<F>, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    /// The view of its relation (its pool's key) through the context.
    [[nodiscard]] decltype(auto) get_relation() const { return self().template member<&urd::record_of<V>::relation>(); }

    /// Its canonical Values.
    [[nodiscard]] decltype(auto) get_values() const { return self().template member<&urd::record_of<V>::values>(); }

    /// The canonical Value of a column; std::out_of_range past the last.
    [[nodiscard]] ygg::Value value(std::size_t column) const
    {
        decltype(auto) values = self().get_values();
        ygg::detail::check_column(column, std::ranges::size(values));
        return values[column];
    }

    /// The type of a column, its relation's; std::out_of_range past the last.
    [[nodiscard]] ygg::TypeId type(std::size_t column) const
    {
        ygg::detail::check_column(column, std::ranges::size(self().get_values()));
        return ygg::detail::with_column_types(get_relation(),
                                              [column](const auto& types) -> ygg::TypeId
                                              { return *std::ranges::next(std::ranges::begin(types), static_cast<std::ptrdiff_t>(column)); });
    }

    /// A column decoded as its type (an object column as its untagged object index).
    [[nodiscard]] ygg::ColumnValue get(std::size_t column) const { return ygg::decode(value(column), type(column)); }

    /// A column as T; throws std::invalid_argument where the column's type is not T's (ygg::type_id).
    template<typename T>
    [[nodiscard]] T get(std::size_t column) const
    {
        ygg::detail::check_column_type<T>(type(column));
        return ygg::decode_value<T>(value(column));
    }

    /// Columns first to last (exclusive) as T, a borrowed random-access range; throws std::invalid_argument unless each
    /// column's type is the one T reads (checked once, here).
    template<typename T>
    [[nodiscard]] ygg::decoded_columns_t<V, T> get(std::size_t first, std::size_t last) const
    {
        const auto& values = self().get_values();
        if (first < last)
        {
            ygg::detail::check_column(last - 1, std::ranges::size(values));  // std::out_of_range past the last
            ygg::detail::with_column_types(get_relation(),
                                           [first, last](const auto& types)
                                           {
                                               for (const ygg::TypeId type : types | std::views::drop(first) | std::views::take(last - first))
                                                   ygg::detail::check_column_type<T>(type);
                                           });
        }
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
            urd::Functions<urd::function<"value", &view_mixin::value, urd::arg<"column">>,
                           urd::function<"type", &view_mixin::type, urd::arg<"column">>,
                           urd::function<"get", static_cast<ygg::ColumnValue (view_mixin::*)(std::size_t) const>(&view_mixin::get), urd::arg<"column">>> {};
        if constexpr (ygg::ResolvesObjects<urd::view_context_t<V>>)
            return reads
                   + urd::Functions<
                       urd::function<"resolve_object",
                                     static_cast<urd::index_view<Object, urd::view_context_t<V>> (view_mixin::*)(std::size_t) const>(
                                         &view_mixin::template resolve<Object>),
                                     urd::arg<"column">>,
                       urd::function<"resolve_objects",
                                     static_cast<ygg::resolved_columns_t<V, Object> (view_mixin::*)(std::size_t, std::size_t) const>(
                                         &view_mixin::template resolve<Object>),
                                     urd::arg<"first">,
                                     urd::arg<"last">>,
                       urd::function<"resolve_objects",
                                     static_cast<ygg::resolved_columns_t<V, Object> (view_mixin::*)() const>(&view_mixin::template resolve<Object>)>> {};
        else
            return reads;
    }
};

/// A row key held in a record resolves to its view, through its context (context.view(key)).
template<typename F>
struct urd::resolution<ygg::RowIndex<F>>
{
    template<typename C>
    static auto of(ygg::RowIndex<F> key, const C& context)
    { return context.view(key); }

    /// Written from the key itself, or from a stored row's view (its index()).
    template<std::same_as<ygg::RowIndex<F>> R>
    static ygg::RowIndex<F> stored(const R& readout) noexcept
    { return readout; }

    template<ygg::StoredRowView<F> V>
    static ygg::RowIndex<F> stored(const V& view) noexcept
    { return view.index(); }
};

#endif
