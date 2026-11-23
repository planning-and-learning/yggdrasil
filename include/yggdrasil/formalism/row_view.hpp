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
#include <urd/elements/children.hpp>
#include <urd/elements/concepts.hpp>
#include <urd/elements/flat.hpp>
#include <urd/elements/view.hpp>
#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>
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
}  // namespace ygg::detail

namespace ygg
{
/// The columns of a row view V read as T (get<T>()): a range of the values it reads (get_values(): a stored row's from
/// its slot, a held row's or a builder's own), each decoded. A class, so that it is formed only where a member that
/// returns it is used (V is incomplete where its mixin is).
template<typename V, typename T>
struct decoded_columns
{
    using values_type = std::remove_cvref_t<decltype(std::declval<const V&>().get_values())>;
    using type = urd::transformed_range<std::ranges::subrange<std::ranges::iterator_t<const values_type>>, T (*)(Value) noexcept>;
};

template<typename V, typename T>
using decoded_columns_t = typename decoded_columns<V, T>::type;

/// The columns of a row view V read as the records of tag T they name (resolve<T>()), through its context.
template<typename V, typename T>
using resolved_columns_t = urd::resolved_range<decoded_columns_t<V, urd::index<T>>, urd::view_context_t<V>>;
}  // namespace ygg

namespace ygg::detail
{
/// The type of a column of a row view's relation (unchecked) and the number of its columns, as its relation gives them
/// (its view's get_column_types(), stored or computed), whichever form its relation is in (urd::visit_view).
template<typename V>
[[nodiscard]] ygg::TypeId column_type(const V& row, std::size_t column)
{
    return urd::visit_view(row.get_relation(),
                           [column](const auto& relation) -> ygg::TypeId
                           { return *std::ranges::next(std::ranges::begin(relation.get_column_types()), static_cast<std::ptrdiff_t>(column)); });
}

template<typename V>
[[nodiscard]] std::size_t column_count(const V& row)
{
    return urd::visit_view(row.get_relation(), [](const auto& relation) -> std::size_t { return std::ranges::size(relation.get_column_types()); });
}
}  // namespace ygg::detail

/// A row's views, in every form: a stored row (urd::index_view<Row<F>, C, urd::key<F, Row<F>>>, read through the packed
/// row its context hands out, PackedRow<F>), an owned row (urd::data<Row<F>>) and a builder (its relation a child, of
/// any form). Its relation (the view of its pool key, or of the form a builder's child holds) and its values, and the
/// typed reads over them: the types of its columns are those its relation's record stores (get_column_types(), read
/// through whichever form the relation is in). The context and the record, where borrowed, must outlive the view and
/// all ranges it gives.
template<typename F, typename V>
struct urd::view_mixin<ygg::Row<F>, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    /// The view of its relation (its pool's key) through the context.
    [[nodiscard]] decltype(auto) get_relation() const { return self().template member<&urd::record_of<V>::relation>(); }

    /// Its canonical Values: a stored row's read from its slot as they are needed (PackedRow<F>).
    [[nodiscard]] decltype(auto) get_values() const { return self().template member<&urd::record_of<V>::values>(); }

    /// The canonical Value of a column; std::out_of_range past the last.
    [[nodiscard]] ygg::Value value(std::size_t column) const
    {
        decltype(auto) values = get_values();
        ygg::detail::check_column(column, std::ranges::size(values));
        return values[column];
    }

    /// The type of a column, as its relation's record stores it; std::out_of_range past the last.
    [[nodiscard]] ygg::TypeId type(std::size_t column) const
    {
        ygg::detail::check_column(column, ygg::detail::column_count(self()));
        return ygg::detail::column_type(self(), column);
    }

    /// A column decoded as its type (an object column as its untagged object index).
    [[nodiscard]] ygg::ColumnValue get(std::size_t column) const { return ygg::decode(value(column), type(column)); }

    /// A column as T; throws std::invalid_argument where the column's type is not T's (ygg::type_id).
    template<typename T>
    [[nodiscard]] T get(std::size_t column) const
    {
        if (type(column) != ygg::type_id<T>)
            throw std::invalid_argument("Row: the column's type is not the requested one.");
        return ygg::decode_value<T>(value(column));
    }

    /// Every column as T, a borrowed random-access range; throws std::invalid_argument unless each column's type is the
    /// one T reads (checked once, here).
    template<typename T>
    [[nodiscard]] ygg::decoded_columns_t<V, T> get() const
    {
        const auto& values = get_values();
        const auto count = std::ranges::size(values);
        if (count > 0)
            ygg::detail::check_column(count - 1, ygg::detail::column_count(self()));  // std::out_of_range past the last
        for (std::size_t column = 0; column < count; ++column)
            if (ygg::detail::column_type(self(), column) != ygg::type_id<T>)
                throw std::invalid_argument("Row: the column's type is not the requested one.");
        return urd::transform(std::ranges::subrange(values.begin(), values.end()), &ygg::decode_value<T>);
    }

    /// The record of type T a column names, resolved through its context (a ground record's objects are its row's,
    /// `atom.get_row().resolve<Object>()`): where its context resolves indices of T (ResolvesColumns). Throws
    /// std::invalid_argument where the column's type is not urd::index<T> (get<T>).
    template<typename T>
        requires ygg::ResolvesColumns<V, T>
    [[nodiscard]] urd::index_view<T, urd::view_context_t<V>> resolve(std::size_t column) const
    { return urd::resolve(get<urd::index<T>>(column), self().context()); }

    /// Every column as the record of type T it names (see resolve<T>(column)).
    template<typename T>
        requires ygg::ResolvesColumns<V, T>
    [[nodiscard]] ygg::resolved_columns_t<V, T> resolve() const
    { return urd::resolve(get<urd::index<T>>(), self().context()); }

    /// Its reads, and where its context resolves objects (ResolvesObjects) their resolutions: resolve_object(column) and
    /// resolve_objects().
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
                                     static_cast<ygg::resolved_columns_t<V, Object> (view_mixin::*)() const>(&view_mixin::template resolve<Object>)>> {};
        else
            return reads;
    }
};

/// A row's values are children where its relation's column type is an object and its context resolves objects, plain
/// values otherwise (urd::element_children): urd's builders read a row through them, so that urd::copy copies a row's
/// objects as children into a repository that does not see them, and keeps every other value as it is.
template<typename F, urd::flat_vector<ygg::Value> urd::data<ygg::Row<F>>::* Member>
struct urd::element_children<Member>
{
    using child_type = urd::index<ygg::formalism::Object>;

    template<typename R, typename C>
    [[nodiscard]] static bool is_child(const R& stored, const C& context, std::size_t position)
    {
        const auto object_column = [position](const auto& relation)
        { return *std::ranges::next(std::ranges::begin(relation.get_column_types()), static_cast<std::ptrdiff_t>(position)) == ygg::type_id<child_type>; };
        if constexpr (ygg::ResolvesObjects<C> && urd::is_builder<R>)
            return urd::visit_view(urd::resolve(stored.relation, context), object_column);  // its relation a child, in any form
        else if constexpr (ygg::ResolvesObjects<C>)
            return object_column(context.view(urd::stored_field<&urd::data<ygg::Row<F>>::relation, R>::read(stored)));
        else
            return false;  // a context that resolves no objects (a free relation's) has no object children: every value is plain
    }

    [[nodiscard]] static child_type child(ygg::Value element) noexcept { return ygg::decode_value<child_type>(element); }

    [[nodiscard]] static ygg::Value element(child_type child) noexcept { return ygg::encode_value(child); }
};

namespace ygg
{
/// A row is serialized as its relation and its columns: each object column as its object (resolve<Object>(column), where
/// its context resolves objects), every other column as get(column) reads it (pyyggdrasil.serialization).
template<class Archive, typename F, typename C>
void describe_fields(Archive& ar, std::type_identity<ygg::RowView<F, C>> /*type*/)
{
    ar.field("relation", [](const auto& value) -> decltype(auto) { return value.get_relation(); });
    ar.field("values",
             [](const auto& value)
             {
                 if constexpr (ygg::ResolvesObjects<C>)
                 {
                     using Column = std::variant<ygg::ColumnValue, urd::index_view<ygg::formalism::Object, C>>;
                     auto columns = urd::Store<urd::sequence<Column>> {};
                     for (std::size_t column = 0; column < std::ranges::size(value.get_values()); ++column)
                     {
                         if (value.type(column) == ygg::type_id<urd::index<ygg::formalism::Object>>)
                             columns.push_back(Column(std::in_place_index<1>, value.template resolve<ygg::formalism::Object>(column)));
                         else
                             columns.push_back(Column(std::in_place_index<0>, value.get(column)));
                     }
                     return columns;
                 }
                 else
                 {
                     auto columns = urd::Store<urd::sequence<ygg::ColumnValue>> {};
                     for (std::size_t column = 0; column < std::ranges::size(value.get_values()); ++column)
                         columns.push_back(value.get(column));
                     return columns;
                 }
             });
}
}  // namespace ygg

#endif
