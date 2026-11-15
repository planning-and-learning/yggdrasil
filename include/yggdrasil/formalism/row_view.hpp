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

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <urd/elements/flat.hpp>
#include <utility>

namespace ygg::detail
{
/// What a row's views V derive from its values, its columns' types and its relation's key (V's get_values(),
/// column_types() and relation_key()): its size, each column typed, each typed read checked against its type, and its
/// logical key.
template<typename F, typename V>
struct row_columns
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
        return urd::transform(std::ranges::subrange(values.begin() + first, values.begin() + last), &decode_value<T>);
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

/// A row's accessors, for a stored row (urd::index_view<Row<F>, C, RowIndex<F>>, its record packed into its pool, read
/// through urd::stored_member below) and a row held outside the repository (urd::borrowed_view of its record) alike: its
/// relation, its values and their types. The context and the record, where borrowed, must outlive the view and all
/// ranges it gives.
template<typename F, typename V>
struct urd::view_mixin<ygg::Row<F>, V> : ygg::detail::row_columns<F, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    /// The view of its relation (its pool's key) through the context.
    [[nodiscard]] decltype(auto) get_relation() const { return self().template member<&urd::data<ygg::Row<F>>::relation>(); }
    /// Its canonical Values.
    [[nodiscard]] decltype(auto) get_values() const { return self().template member<&urd::data<ygg::Row<F>>::values>(); }
    /// Its relation's key.
    [[nodiscard]] urd::index<F> relation_key() const { return get_relation().index(); }
    /// Its columns' types: its pool's.
    [[nodiscard]] std::span<const ygg::TypeId> column_types() const { return get_repository(self().context()).types(relation_key()); }
};

/// A stored row's members, its record packed into its pool: its relation is its key's, its values the context decodes.
template<typename F>
struct urd::stored_member<ygg::Row<F>>
{
    template<typename V>
    static auto of(const V& view, urd::member_tag<&urd::data<ygg::Row<F>>::relation> /*relation*/)
    { return view.context().view(view.index().relation); }
    template<typename V>
    static auto of(const V& view, urd::member_tag<&urd::data<ygg::Row<F>>::values> /*values*/)
    { return view.context().values(view.index()); }
};

namespace ygg
{
/// A row of a relation builder, read through the builder by its position: its values and its columns' types. It is no
/// row of a stored relation, so it has no relation (its logical key's is none). The builder must outlive it; the
/// builder's next mutation invalidates it.
template<typename F>
class RowBuilderView : public detail::row_columns<F, RowBuilderView<F>>
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
};

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

#endif
