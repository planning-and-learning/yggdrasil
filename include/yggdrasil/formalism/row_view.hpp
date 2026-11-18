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

    /// Its columns' types: its relation's (its pool's key's get_column_types()), read in whatever form the relation is
    /// held (interned, built, a record not interned yet): a borrowed random-access range.
    [[nodiscard]] auto column_types() const
    {
        return urd::transform(std::views::iota(std::size_t { 0 }, std::ranges::size(self().get_values())),
                              &self(),
                              [](const V* row, std::size_t column) { return row->type(column); });
    }

    /// The canonical Value of a column; std::out_of_range past the last.
    [[nodiscard]] ygg::Value value(std::size_t column) const
    { return self().get_values()[ygg::detail::checked_column(column, std::ranges::size(self().get_values()))]; }

    /// The type of a column, its relation's; std::out_of_range past the last.
    [[nodiscard]] ygg::TypeId type(std::size_t column) const
    {
        const auto checked = ygg::detail::checked_column(column, std::ranges::size(self().get_values()));
        return urd::visit_view(get_relation(),
                               [checked](const auto& relation) -> ygg::TypeId
                               {
                                   const auto& types = relation.get_column_types();
                                   return std::ranges::begin(types)[static_cast<std::ptrdiff_t>(checked)];
                               });
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
    [[nodiscard]] auto get(std::size_t first, std::size_t last) const
    {
        for (std::size_t i = first; i < last; ++i)
            ygg::detail::check_column_type<T>(self().type(i));
        decltype(auto) values = self().get_values();
        return urd::transform(std::ranges::subrange(std::ranges::next(values.begin(), static_cast<std::ptrdiff_t>(first)),
                                                    std::ranges::next(values.begin(), static_cast<std::ptrdiff_t>(last))),
                              &ygg::decode_value<T>);
    }

    /// Every column as T (see get<T>(first, last)).
    template<typename T>
    [[nodiscard]] auto get() const
    { return get<T>(0, std::ranges::size(self().get_values())); }

    /// Its columns as objects, resolved through its context: where its family's rows hold objects (a ground record's
    /// objects are its row's, `atom.get_row().get_objects()`).
    [[nodiscard]] auto get_objects() const
        requires ygg::formalism::ObjectRows<F>
    { return urd::resolve(get<urd::index<ygg::formalism::Object>>(), self().context()); }

    /// Its reads, and get_objects where its family's rows hold objects (ObjectRows).
    static constexpr auto functions() noexcept
    {
        constexpr auto reads =
            urd::Functions<urd::function<"column_types", &view_mixin::column_types>,
                           urd::function<"value", &view_mixin::value, urd::arg<"column">>,
                           urd::function<"type", &view_mixin::type, urd::arg<"column">>,
                           urd::function<"get", static_cast<ygg::ColumnValue (view_mixin::*)(std::size_t) const>(&view_mixin::get), urd::arg<"column">>> {};
        if constexpr (ygg::formalism::ObjectRows<F>)
            return reads + urd::Functions<urd::function<"get_objects", &view_mixin::get_objects>> {};
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
