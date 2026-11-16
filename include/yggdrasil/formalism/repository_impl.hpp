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

#ifndef YGG_FORMALISM_REPOSITORY_IMPL_HPP_
#define YGG_FORMALISM_REPOSITORY_IMPL_HPP_

// The definitions of Repository (repository.hpp): its stores (Impl) and every member. Included by the source that
// instantiates a library's repositories (repository_instantiations.hpp), by code that reaches into the symbol or relation
// repository that is not a library's (yggdrasil's own tests).

#include "yggdrasil/core/family.hpp"
#include "yggdrasil/formalism/relation_repository.hpp"
#include "yggdrasil/formalism/repository.hpp"
#include "yggdrasil/formalism/symbol_repository.hpp"
#include "yggdrasil/semantics/canonicalization.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <urd/repr/containers/schema_rows.hpp>
#include <urd/repr/sequences/sequence.hpp>
#include <urd/repr/slots/slots.hpp>
#include <urd/store.hpp>
#include <utility>

namespace ygg::formalism
{

template<typename SymbolRepo, typename RelationRepo>
struct Repository<SymbolRepo, RelationRepo>::Impl
{
    /// The builders get_relation_builder hands out for family F, by their column widths: a builder handed back is
    /// reused only for equal widths, so its row storage stays.
    template<typename F>
    using BuilderSlots = urd::Store<urd::slots<urd::builder<Relation<F>>>, urd::keyed<urd::column_widths>>;
    template<typename F>
    using RelationFamily = boost::mp11::mp_contains<SymbolTypes, Relation<F>>;
    template<typename... Fs>
    using BuilderFamily = Family<BuilderSlots, Fs...>;
    using RelationFamilies = boost::mp11::mp_copy_if<RelationTypes, RelationFamily>;
    /// A pool key's records stay across clear(), and so do their pools.
    template<typename T>
    using KeysPools = boost::mp11::mp_contains<RelationTypes, T>;

    /// The stores of what the repository holds (declarations.hpp, Symbols and Relations).
    using SymbolStore = typename detail::symbol_repository_traits<SymbolRepo>::Store;
    using RelationStore = typename detail::relation_repository_traits<RelationRepo>::Store;

    SymbolStore symbols;
    RelationStore relations;
    /// Schema records to build in (reused: interning one allocates nothing once warm); threads take their own.
    urd::Store<urd::slots<urd::data<Schema>>, std::conditional_t<RelationStore::thread_safe, urd::concurrent, urd::single_threaded>> schema_builders;
    /// The builder slots of each family with relations (none where the layer has no relations).
    mutable boost::mp11::mp_apply<BuilderFamily, RelationFamilies> relation_builders;

    Impl(const Repository* parent, urd::bit_width object_index_width) :
        symbols(parent ? &parent->m_impl->symbols : nullptr),
        relations(parent ? &parent->m_impl->relations : nullptr, object_index_width)
    {
    }

    static urd::bit_width object_index_width(std::size_t num_objects, const Repository* parent)
    {
        if (num_objects > std::numeric_limits<ygg::uint_t>::max())
            throw std::invalid_argument("Repository: number of objects exceeds the object index domain.");
        auto width = urd::bit_width::for_bound(num_objects);
        if (parent)
            width.bits = std::max(width.bits, parent->m_impl->relations.get_object_index_width());
        return width;
    }

    /// Whether some layer holds the row `key`.
    template<typename F>
    [[nodiscard]] bool holds_row(RowIndex<F> key) const
    {
        const auto& pools = relations.template get<F>();
        return pools.contains(key.relation) && key.row.value < pools.at(key.relation).size();
    }

    /// The Schema of these column types, interned here (built in a reused record).
    template<std::ranges::input_range Types>
    urd::index<Schema> intern_schema(const Types& types)
    {
        auto record = schema_builders.make_unique();
        urd::clear_members(*record);
        for (const TypeId type : types)
            record->types.push_back(type);
        return relations.schema(*record);
    }
};

template<typename SymbolRepo, typename RelationRepo>
Repository<SymbolRepo, RelationRepo>::Repository(const Repository* parent) :
    m_parent(parent),
    m_layer(next_layer()),
    m_impl(std::make_unique<Impl>(parent, default_object_index_width))
{
}

template<typename SymbolRepo, typename RelationRepo>
Repository<SymbolRepo, RelationRepo>::Repository(std::size_t num_objects, const Repository* parent) :
    m_parent(parent),
    m_layer(next_layer()),
    m_impl(std::make_unique<Impl>(parent, Impl::object_index_width(num_objects, parent)))
{
}

template<typename SymbolRepo, typename RelationRepo>
Repository<SymbolRepo, RelationRepo>::~Repository()  // NOLINT(modernize-use-equals-default): GCC rejects an explicit instantiation of an out-of-line = default
{
}

template<typename SymbolRepo, typename RelationRepo>
void Repository<SymbolRepo, RelationRepo>::clear()
{
    boost::mp11::mp_for_each<boost::mp11::mp_transform<std::type_identity, boost::mp11::mp_remove_if<SymbolTypes, Impl::template KeysPools>>>(
        [this]<typename T>(std::type_identity<T>) { m_impl->symbols.template get<T>().clear(); });
    m_impl->relations.clear();
}

template<typename SymbolRepo, typename RelationRepo>
void Repository<SymbolRepo, RelationRepo>::clear_all()
{
    m_impl->symbols.clear();
    m_impl->relations.clear();
}

template<typename SymbolRepo, typename RelationRepo>
template<typename T>
bool Repository<SymbolRepo, RelationRepo>::do_contains(urd::index<T> index) const
{ return m_impl->symbols.contains(index); }

template<typename SymbolRepo, typename RelationRepo>
template<typename T>
std::size_t Repository<SymbolRepo, RelationRepo>::do_size() const
{ return m_impl->symbols.template size<T>(); }

template<typename SymbolRepo, typename RelationRepo>
template<typename T>
std::size_t Repository<SymbolRepo, RelationRepo>::do_memory_usage() const
{ return m_impl->symbols.template memory_usage<T>(); }

template<typename SymbolRepo, typename RelationRepo>
template<typename T>
const Repository<SymbolRepo, RelationRepo>* Repository<SymbolRepo, RelationRepo>::do_owner(urd::index<T> index) const
{
    if (!m_impl->symbols.contains(index))
        return nullptr;
    const auto* store = &m_impl->symbols.template get<T>().owner(index);
    const auto* layer = this;
    while (&layer->m_impl->symbols.template get<T>() != store)
    {
        layer = layer->m_parent;
        assert(layer != nullptr && "the layer that holds the index is below this one");
    }
    return layer;
}

template<typename SymbolRepo, typename RelationRepo>
template<typename F>
const Repository<SymbolRepo, RelationRepo>* Repository<SymbolRepo, RelationRepo>::do_owner(RowIndex<F> key) const
{
    if (key.relation.is_none())
        return nullptr;
    // One walk up the layers: the nearest own pool bounds the rows; the first whose own rows start at or below the row
    // holds it (a pool layers over the nearest ancestor's).
    auto bounded = false;
    for (const auto* layer = this; layer != nullptr; layer = layer->m_parent)
    {
        const auto* rows = layer->m_impl->relations.template get<F>().own_value(key.relation);
        if (rows == nullptr)
            continue;
        if (!bounded && (key.row.value) >= rows->size())
            return nullptr;
        bounded = true;
        if ((key.row.value) >= rows->base())
            return layer;
    }
    return nullptr;
}

template<typename SymbolRepo, typename RelationRepo>
template<typename T>
std::optional<urd::index_view<T, Repository<SymbolRepo, RelationRepo>>> Repository<SymbolRepo, RelationRepo>::do_find(const urd::data<T>& builder) const
{
    if (const auto index = m_impl->symbols.template get<T>().find(builder); !index.is_none())
        return view(index);
    return std::nullopt;
}

template<typename SymbolRepo, typename RelationRepo>
template<typename T>
std::pair<urd::index_view<T, Repository<SymbolRepo, RelationRepo>>, bool> Repository<SymbolRepo, RelationRepo>::do_insert(urd::data<T>& builder)
{
    const auto [index, created] = intern(m_impl->symbols.template get<T>(), builder);
    const auto key = view(index);
    if constexpr (HasRows<Repository, T>)
        m_impl->relations.declare(index, m_impl->intern_schema(key.get_column_types()));
    return { key, created };
}

template<typename SymbolRepo, typename RelationRepo>
template<typename F>
std::span<const TypeId> Repository<SymbolRepo, RelationRepo>::do_types(urd::index<F> relation) const
{ return m_impl->relations.types(relation); }

template<typename SymbolRepo, typename RelationRepo>
template<typename T>
const urd::data<T>& Repository<SymbolRepo, RelationRepo>::do_at(urd::index<T> index) const
{
    if (!m_impl->symbols.contains(index))
        throw std::out_of_range("Symbol index not found in any repository layer.");
    return m_impl->symbols[index];
}

template<typename SymbolRepo, typename RelationRepo>
template<typename T>
urd::index_view<T, Repository<SymbolRepo, RelationRepo>> Repository<SymbolRepo, RelationRepo>::do_view(urd::index<T> index) const
{
    if (const auto* layer = owner(index))
        return { index, layer };
    throw std::out_of_range("Symbol index not found in any repository layer.");
}

template<typename SymbolRepo, typename RelationRepo>
const urd::data<Schema>& Repository<SymbolRepo, RelationRepo>::operator[](urd::index<Schema> index) const
{
    if (!m_impl->relations.contains(index))
        throw std::out_of_range("Schema index not found in any repository layer.");
    return m_impl->relations[index];
}

template<typename SymbolRepo, typename RelationRepo>
urd::index_view<Schema, Repository<SymbolRepo, RelationRepo>> Repository<SymbolRepo, RelationRepo>::view(urd::index<Schema> index) const
{
    if (!m_impl->relations.contains(index))
        throw std::out_of_range("Schema index not found in any repository layer.");
    const auto* store = &m_impl->relations.schemas().owner(index);
    const auto* layer = this;
    while (&layer->m_impl->relations.schemas() != store)
        layer = layer->m_parent;
    return { index, layer };
}

template<typename SymbolRepo, typename RelationRepo>
template<typename F>
std::optional<RowView<F, Repository<SymbolRepo, RelationRepo>>> Repository<SymbolRepo, RelationRepo>::do_find(const urd::data<Row<F>>& row) const
{
    if (const auto id = m_impl->relations.find(row.relation, row.values); !id.is_none())
        return view(RowIndex<F> { row.relation, id });
    return std::nullopt;
}

template<typename SymbolRepo, typename RelationRepo>
template<typename F>
std::pair<RowView<F, Repository<SymbolRepo, RelationRepo>>, bool> Repository<SymbolRepo, RelationRepo>::do_insert(const urd::data<Row<F>>& row)
{
    const auto [id, created] = m_impl->relations.insert(row.relation, row.values);
    const auto key = RowIndex<F> { row.relation, id };
    return { created ? RowView<F, Repository>(key, this) : view(key), created };
}

template<typename SymbolRepo, typename RelationRepo>
template<typename F>
urd::index<Row<F>> Repository<SymbolRepo, RelationRepo>::do_find_row(urd::index<F> relation, std::span<const Value> values) const
{ return m_impl->relations.find(relation, values); }

template<typename SymbolRepo, typename RelationRepo>
template<typename F>
urd::index<Row<F>> Repository<SymbolRepo, RelationRepo>::do_find_row(urd::index<F> relation, const StoredRow& values) const
{ return m_impl->relations.find(relation, values); }

template<typename SymbolRepo, typename RelationRepo>
template<typename F>
std::size_t Repository<SymbolRepo, RelationRepo>::do_size(urd::index<F> relation) const
{ return m_impl->relations.size(relation); }

template<typename SymbolRepo, typename RelationRepo>
template<typename F>
std::size_t Repository<SymbolRepo, RelationRepo>::do_row_memory_usage() const
{ return m_impl->relations.template memory_usage<F>(); }

template<typename SymbolRepo, typename RelationRepo>
template<typename F>
std::pair<urd::index<Row<F>>, bool> Repository<SymbolRepo, RelationRepo>::do_insert_row(const urd::data<Row<F>>& row)
{ return m_impl->relations.insert(row.relation, row.values); }

template<typename SymbolRepo, typename RelationRepo>
template<typename F>
typename Repository<SymbolRepo, RelationRepo>::StoredRow Repository<SymbolRepo, RelationRepo>::do_at(RowIndex<F> key) const
{
    const auto& rows = m_impl->relations.template get<F>().at(key.relation);
    if ((key.row.value) >= rows.size())
        throw std::out_of_range("Row index not found in any repository layer.");
    static_assert(std::same_as<decltype(rows.at(key.row)), StoredRow>, "Repository::StoredRow is what a stored row reads as");
    return rows.at(key.row);
}

template<typename SymbolRepo, typename RelationRepo>
template<typename F>
RowView<F, Repository<SymbolRepo, RelationRepo>> Repository<SymbolRepo, RelationRepo>::do_view(RowIndex<F> key) const
{
    if (const auto* layer = owner(key))
        return { key, layer };
    throw std::out_of_range("Row index not found in any repository layer.");
}

template<typename SymbolRepo, typename RelationRepo>
urd::index<Schema> Repository<SymbolRepo, RelationRepo>::do_schema(std::span<const ColumnLayout> columns)
{
    const auto key = m_impl->intern_schema(columns | std::views::transform(&ColumnLayout::type));
    m_impl->relations.declare(key, key);
    return key;
}

template<typename SymbolRepo, typename RelationRepo>
urd::index<Schema> Repository<SymbolRepo, RelationRepo>::do_schema(std::span<const TypeId> types)
{
    const auto key = m_impl->intern_schema(types);
    m_impl->relations.declare(key, key);
    return key;
}

template<typename SymbolRepo, typename RelationRepo>
template<typename F>
std::pair<urd::index_view<Relation<F>, Repository<SymbolRepo, RelationRepo>>, bool>
Repository<SymbolRepo, RelationRepo>::do_insert(urd::data<Relation<F>>& relation)
{
    const auto& values = (*this)[relation.columns].values;
    const auto columns = std::span<const ColumnLayout>(values.data(), values.size());
    if (!std::ranges::equal(m_impl->relations.template types<F>(relation.pool), columns, {}, {}, &ColumnLayout::type))
        throw std::invalid_argument("Repository: the columns' types are not the pool's.");
    for (auto it = columns.begin(); it != columns.end(); ++it)
        if (repeats_label(columns, it))
            throw std::invalid_argument("Repository: a relation's column labels are distinct.");
    // ponytail: an assert, not a throw: O(rows) per insert (user). Catches ids beyond the pool (e.g. of another
    // repository), not an id that happens to exist in both.
    assert(std::ranges::all_of(relation.rows, [&](RowIndex<F> key) { return key.relation == relation.pool && m_impl->holds_row(key); })
           && "Repository: a relation's rows must be rows of its pool here.");
    ygg::canonicalize(relation.rows);
    const auto [index, created] = intern(m_impl->symbols.template get<Relation<F>>(), relation);
    return { view(index), created };
}

template<typename SymbolRepo, typename RelationRepo>
template<typename F>
std::shared_ptr<urd::builder<Relation<F>>> Repository<SymbolRepo, RelationRepo>::do_get_relation_builder(std::span<const ColumnLayout> columns) const
{
    auto builder =
        m_impl->relation_builders.template get<F>().make_shared(m_impl->relations.widths(columns | std::views::transform(&ColumnLayout::type)), columns);
    builder->initialize(columns);  // a builder handed back holds its last columns and rows
    return builder;
}

}  // namespace ygg::formalism

#endif
