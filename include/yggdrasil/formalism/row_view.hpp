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
#include <urd/elements/flat.hpp>
#include <utility>


/// A row's accessors, for a stored row (urd::index_view<Row<F>, C, RowIndex<F>>, which reads the StoredRowRecord its
/// context hands out) and a row held outside the repository (urd::borrowed_view of its record) alike: its
/// relation, its values and their types. The context and the record, where borrowed, must outlive the view and all
/// ranges it gives.
template<typename F, typename V>
struct urd::view_mixin<ygg::Row<F>, V> : ygg::detail::row_reads<F, V>
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
};

namespace ygg
{
/// A row of a relation builder, read through the builder by its position: its values and its columns' types. It is no
/// row of a stored relation, so it has no relation (its logical key's is none). The builder must outlive it; the
/// builder's next mutation invalidates it.
template<typename F>
class RowBuilderView : public detail::row_reads<F, RowBuilderView<F>>
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
    template<typename V>
        requires std::same_as<std::remove_cvref_t<decltype(std::declval<const V&>().index())>, ygg::RowIndex<F>>
    static ygg::RowIndex<F> stored(const V& view) noexcept
    { return view.index(); }
};


#endif
