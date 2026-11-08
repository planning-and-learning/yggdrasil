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
#include "yggdrasil/formalism/relation_data.hpp"
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
/// context[key] is the stored row): its relation, its values and their types. The types are its pool's
/// (relations().types(relation) of a layer; a builder's own), so every typed read is checked against them. A borrowed
/// record and the context must outlive the view and all ranges it gives; keep the record unchanged while used.
template<typename F, typename V>
struct urd::view_mixin<ygg::Row<F>, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }
    template<typename C>
    static std::span<const ygg::Value> values_of(const urd::data<ygg::Row<F>>& record, const C&) noexcept
    { return { record.values.data(), record.values.size() }; }
    template<typename C>
    static auto values_of(ygg::RowIndex<F> key, const C& context)
    { return context[key]; }
    /// The column types of the row's pool: a layer's stored ones, a builder's own (its rows have no relation).
    template<typename C>
    static std::span<const ygg::TypeId> types_of(const C& context, urd::index<F> relation)
    { return get_repository(context).relations().types(relation); }
    static auto types_of(const urd::builder<ygg::Relation<F>>& builder, urd::index<F> /*relation*/) noexcept
    { return builder.columns() | std::views::transform(&ygg::ColumnLayout::type); }
    /// A random-access range of them.
    [[nodiscard]] auto types() const { return types_of(self().context(), self().data().relation); }
    template<typename T>
    static void check(ygg::TypeId type)
    {
        if (type != ygg::type_id<T>)
            throw std::invalid_argument("Row: the column's type is not the requested one.");
    }

public:
    /// The view of its relation (its pool's key) through the context.
    [[nodiscard]] auto get_relation() const { return self().context().view(self().data().relation); }
    /// Its canonical Values as held: a borrowed record's, or the stored row.
    [[nodiscard]] auto get_values() const { return values_of(self().data(), self().context()); }
    [[nodiscard]] std::size_t size() const { return std::ranges::size(get_values()); }
    /// The canonical Value of column i.
    [[nodiscard]] ygg::Value value(std::size_t i) const { return get_values()[i]; }
    /// The type of column i, its pool's.
    [[nodiscard]] ygg::TypeId type(std::size_t i) const { return types()[i]; }
    /// Column i decoded as its type (an object column as its untagged object index).
    [[nodiscard]] ygg::ColumnValue get(std::size_t i) const { return ygg::decode(value(i), type(i)); }
    /// Column i as T; throws std::invalid_argument where the column's type is not T's (ygg::type_id).
    template<typename T>
    [[nodiscard]] T get(std::size_t i) const
    {
        check<T>(type(i));
        return ygg::decode_value<T>(value(i));
    }
    /// Columns first to last (exclusive) as T, a borrowed random-access range; throws std::invalid_argument unless each
    /// column's type is the one T reads (checked once, here).
    template<typename T>
    [[nodiscard]] auto get(std::size_t first, std::size_t last) const
    {
        const auto types = this->types();
        for (std::size_t i = first; i < last; ++i)
            check<T>(types[i]);
        const auto values = get_values();
        return urd::transform(std::ranges::subrange(values.begin() + first, values.begin() + last), &ygg::decode_value<T>);
    }
    /// Every column as T (see get<T>(first, last)).
    template<typename T>
    [[nodiscard]] auto get() const
    { return get<T>(0, size()); }
    /// The logical row across repositories: the relation index (a view's identity includes its context) and the
    /// canonical values.
    [[nodiscard]] auto get_key() const { return std::make_pair(self().data().relation, get_values()); }
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
    { view.type(i) } -> std::same_as<TypeId>;
    { view.get(i) } -> std::same_as<ColumnValue>;
    requires std::same_as<std::remove_cvref_t<decltype(view.get_key().first)>, urd::index<F>>;
    requires std::ranges::forward_range<const std::remove_reference_t<decltype(view.get_key().second)>>
                 && std::ranges::sized_range<const std::remove_reference_t<decltype(view.get_key().second)>>
                 && std::same_as<std::ranges::range_value_t<const std::remove_reference_t<decltype(view.get_key().second)>>, Value>;
};
}  // namespace ygg

#endif
