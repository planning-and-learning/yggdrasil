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

#include <urd/elements/flat.hpp>
#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/row_data.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

/// A row, borrowed (its record {relation, values}) or stored (its key {relation, row}, read through its context:
/// context[key] is the stored row): its relation, values and objects. A borrowed record and the context must outlive
/// the view and all ranges it gives; keep the record unchanged while used.
template<typename F, typename V>
struct urd::view_mixin<ygg::Row<F>, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }
    template<typename C>
    static std::span<const ygg::row_value_t<F>> values_of(const urd::data<ygg::Row<F>>& record, const C&) noexcept
    { return { record.values.data(), record.values.size() }; }
    template<typename C>
    static auto values_of(ygg::RowIndex<F> key, const C& context)
    { return context[key]; }
    template<typename C>
    static std::span<const ygg::row_value_t<F>> typed_values_of(const urd::data<ygg::Row<F>>& record, const C&) noexcept
    { return { record.values.data(), record.values.size() }; }
    template<typename C>
    static auto typed_values_of(ygg::RowIndex<F> key, const C& context)
    { return context[key].template as<ygg::row_value_t<F>>(); }
    /// A value as its canonical Value: a Value as it is, an object index its value.
    static ygg::Value to_value(ygg::Value value) noexcept { return value; }
    template<typename Tag>
    static ygg::Value to_value(urd::index<ygg::formalism::Object<Tag>> object) noexcept
    { return object.value; }
    /// The type of column i: a free relation's from its builder (its rows have no relation) or its Schema record; an
    /// object family's an object.
    template<typename C>
    static ygg::TypeId type_of(const C& context, urd::index<ygg::Schema> relation, std::size_t i)
    { return context[relation].types[i]; }
    static ygg::TypeId type_of(const urd::builder<ygg::Relation<ygg::Schema>>& builder, urd::index<ygg::Schema> /*relation*/, std::size_t i)
    { return builder.columns()[i].type; }
    template<typename C, typename G>
    static ygg::TypeId type_of(const C& /*context*/, urd::index<G> /*relation*/, std::size_t /*i*/)
    { return ygg::type_id<ygg::ObjectIndex>; }
    /// The objects of typed values: resolved through the context (object indices), or a free relation's raw ObjectIndex
    /// values (its layer has no objects).
    template<typename Values, typename C>
        requires std::same_as<std::ranges::range_value_t<Values>, ygg::Value>
    static auto objects_of(const Values& values, const C& /*context*/)
    { return urd::transform(values, [](ygg::Value value) { return ygg::ObjectIndex { static_cast<std::uint32_t>(value) }; }); }
    template<typename Values, typename C>
        requires(!std::same_as<std::ranges::range_value_t<Values>, ygg::Value>)
    static auto objects_of(const Values& values, const C& context)
    { return urd::resolve(values, context); }

public:
    /// The view of its relation (its pool's key) through the context.
    [[nodiscard]] auto get_relation() const { return self().context().view(self().data().relation); }
    /// Its values as held: a borrowed record's, or the stored row (canonical Values).
    [[nodiscard]] auto get_values() const { return values_of(self().data(), self().context()); }
    [[nodiscard]] std::size_t size() const { return std::ranges::size(get_values()); }
    /// The canonical Value of column i.
    [[nodiscard]] ygg::Value value(std::size_t i) const { return to_value(get_values()[i]); }
    /// The type of column i: an object family's columns are objects; a free relation's are its Schema's.
    [[nodiscard]] ygg::TypeId type(std::size_t i) const { return type_of(self().context(), self().data().relation, i); }
    /// Column i decoded as its type (an object column as its raw ObjectIndex). Named so because urd's views own get<>(),
    /// which hides a mixin's get.
    [[nodiscard]] ygg::ColumnValue column_value(std::size_t i) const { return ygg::decode(value(i), type(i)); }
    /// Column i as T; throws std::invalid_argument where the column's type is not T's.
    template<ygg::ColumnValueType T>
    [[nodiscard]] T column_value(std::size_t i) const
    {
        if (type(i) != ygg::type_id<T>)
            throw std::invalid_argument("Row: the column's type is not the requested one.");
        return ygg::decode_value<T>(value(i));
    }
    /// Its objects; throws std::invalid_argument unless every column is an object column. An object family's are resolved
    /// through the context; a free relation's are raw ObjectIndex values.
    [[nodiscard]] auto get_objects() const
    {
        for (std::size_t i = 0, n = size(); i < n; ++i)
            if (type(i) != ygg::type_id<ygg::ObjectIndex>)
                throw std::invalid_argument("Row: get_objects() requires object columns only.");
        return objects_of(typed_values_of(self().data(), self().context()), self().context());
    }
    /// The logical row across repositories: the relation index (a view's identity includes its context) and the values,
    /// typed (row_value_t<F>).
    [[nodiscard]] auto get_key() const { return std::make_pair(self().data().relation, typed_values_of(self().data(), self().context())); }
};

/// A stored row's view reads as a borrowed one.
template<typename F, typename V>
struct urd::view_mixin<ygg::RowIndex<F>, V> : urd::view_mixin<ygg::Row<F>, V>
{
};

/// A row key held in a record resolves to its view, through its context (context.view(key)).
template<typename F>
struct urd::resolution<ygg::RowIndex<F>>
{
    template<typename C>
    static auto of(ygg::RowIndex<F> key, const C& context)
    { return context.view(key); }
};

namespace ygg
{
/// The shared read interface of borrowed and stored rows of family F. Identity is deliberately absent; a logical key
/// consists of the relation and the values.
template<typename V, typename F>
concept RowViewConcept = requires(const std::remove_reference_t<V>& view, std::size_t i) {
    view.data();
    view.context();
    { view.size() } -> std::same_as<std::size_t>;
    { view.value(i) } -> std::same_as<Value>;
    requires std::same_as<std::remove_cvref_t<decltype(view.get_key().first)>, urd::index<F>>;
    requires std::ranges::forward_range<const std::remove_reference_t<decltype(view.get_key().second)>>
                 && std::ranges::sized_range<const std::remove_reference_t<decltype(view.get_key().second)>>
                 && std::same_as<std::ranges::range_value_t<const std::remove_reference_t<decltype(view.get_key().second)>>, row_value_t<F>>;
};
}  // namespace ygg

#endif
