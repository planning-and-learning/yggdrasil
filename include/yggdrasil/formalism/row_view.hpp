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
#include "yggdrasil/formalism/view_reads.hpp"

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

namespace ygg
{
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

    /// Its relation's key.
    [[nodiscard]] urd::index<F> relation_key() const { return get_relation().index(); }

    /// Its columns' types: its pool's.
    [[nodiscard]] std::span<const ygg::TypeId> column_types() const { return get_repository(self().context()).types(relation_key()); }

    /// The canonical Value of column i; std::out_of_range past the last.
    [[nodiscard]] ygg::Value value(std::size_t i) const { return self().get_values()[ygg::detail::checked_column(i, std::ranges::size(self().get_values()))]; }

    /// The type of column i, its pool's; std::out_of_range past the last.
    [[nodiscard]] ygg::TypeId type(std::size_t i) const
    { return self().column_types()[ygg::detail::checked_column(i, std::ranges::size(self().get_values()))]; }

    /// Column i decoded as its type (an object column as its untagged object index).
    [[nodiscard]] ygg::ColumnValue get(std::size_t i) const { return ygg::decode(value(i), type(i)); }

    /// Column i as T; throws std::invalid_argument where the column's type is not T's (ygg::type_id).
    template<typename T>
    [[nodiscard]] T get(std::size_t i) const
    {
        ygg::detail::check_column_type<T>(type(i));
        return ygg::decode_value<T>(value(i));
    }

    /// Columns first to last (exclusive) as T, a borrowed random-access range; throws std::invalid_argument unless each
    /// column's type is the one T reads (checked once, here).
    template<typename T>
    [[nodiscard]] auto get(std::size_t first, std::size_t last) const
    {
        const auto types = self().column_types();
        for (std::size_t i = first; i < last; ++i)
            ygg::detail::check_column_type<T>(types[i]);
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
        constexpr auto reads = urd::Functions<urd::function<"relation_key", &view_mixin::relation_key>,
                                              urd::function<"column_types", &view_mixin::column_types>,
                                              urd::function<"value", &view_mixin::value>,
                                              urd::function<"type", &view_mixin::type>,
                                              urd::function<"get", static_cast<ygg::ColumnValue (view_mixin::*)(std::size_t) const>(&view_mixin::get)>> {};
        if constexpr (ygg::formalism::ObjectRows<F>)
            return reads + urd::Functions<urd::function<"get_objects", &view_mixin::get_objects>> {};
        else
            return reads;
    }
};

namespace ygg
{
/// A row of a relation builder, read through the builder by its position: its values and its columns' types. It is no
/// row of a stored relation, so it has no relation (its logical key's is none). The builder must outlive it; the
/// builder's next mutation invalidates it.
template<typename F>
class RowBuilderView : public urd::view_mixin<Row<F>, RowBuilderView<F>>
{
public:
    RowBuilderView(const urd::builder<Relation<F>>* builder, RowIndex<F> key) noexcept : m_builder(builder), m_key(key) {}

    /// The builder it is read through.
    [[nodiscard]] const urd::builder<Relation<F>>& context() const noexcept { return *m_builder; }

    /// Its canonical Values.
    [[nodiscard]] auto get_values() const noexcept { return m_builder->values(m_key); }

    /// Its relation's key: none.
    [[nodiscard]] urd::index<F> relation_key() const noexcept { return {}; }

    /// Its columns' types: the builder's.
    [[nodiscard]] auto column_types() const { return m_builder->columns() | std::views::transform(&ColumnLayout::type); }

private:
    const urd::builder<Relation<F>>* m_builder;
    RowIndex<F> m_key;
};
}  // namespace ygg

namespace ygg
{
/// Whether V is the view of a stored row of family F: its index() is the row's key.
template<typename V, typename F>
concept StoredRowView = requires(const V& view) {
    { view.index() } -> std::same_as<RowIndex<F>>;
};
}  // namespace ygg

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
