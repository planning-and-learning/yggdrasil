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
#include "yggdrasil/formalism/canonicalization.hpp"
#include "yggdrasil/formalism/keyed_impl.hpp"
#include "yggdrasil/formalism/relation_builder.hpp"
#include "yggdrasil/formalism/relation_repository.hpp"
#include "yggdrasil/formalism/repository.hpp"
#include "yggdrasil/formalism/symbol_repository.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <urd/repr/containers/schema_rows.hpp>
#include <urd/repr/sequences/sequence.hpp>
#include <urd/repr/slots/slots.hpp>
#include <urd/store.hpp>
#include <utility>

namespace ygg::formalism
{

/// Its relation repository written through: the fold's step for a row's relation (own: its slot, made on demand, with
/// the widths of its relation symbol's column types, read through the repository) and for a Schema (get: the Schemas).
template<typename SymbolRepo, typename RelationRepo>
struct Repository<SymbolRepo, RelationRepo>::RelationSlots
{
    Repository* repository;

    template<typename F>
        requires HasRows<Repository, F>
    detail::RowSlot<F, typename detail::relation_repository_traits<RelationRepo>::Store::template Rows<F>> own(urd::index<F> relation);

    template<typename T>
        requires boost::mp11::mp_contains<typename detail::relation_repository_traits<RelationRepo>::Store::RecordTypes, T>::value
    typename detail::relation_repository_traits<RelationRepo>::Store::Schemas& get();
};

template<typename SymbolRepo, typename RelationRepo>
struct Repository<SymbolRepo, RelationRepo>::Impl
{
    /// A pool key's records stay across clear(), and so do their pools.
    template<typename T>
    using KeysPools = boost::mp11::mp_contains<RelationTypes, T>;

    /// The stores of what the repository holds (declarations.hpp, Symbols and Relations).
    using SymbolStore = typename detail::symbol_repository_traits<SymbolRepo>::Store;
    using RelationStore = typename detail::relation_repository_traits<RelationRepo>::Store;

    SymbolStore symbols;
    RelationStore relations;
    /// Its relations as records are written into them (OwnWritableParts).
    RelationSlots slots;

    /// Schema records to build in (reused: interning one allocates nothing once warm); threads take their own.
    urd::Store<urd::slots<urd::data<Schema>>, std::conditional_t<RelationStore::thread_safe, urd::concurrent, urd::single_threaded>> schema_builders;

    Impl(Repository& repository, const Repository* parent, urd::bit_width object_index_width) :
        symbols(parent ? &parent->m_impl->symbols : nullptr),
        relations(parent ? &parent->m_impl->relations : nullptr, object_index_width),
        slots { &repository }
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

    /// The Schema of these column types, interned here (built in a reused record).
    template<std::ranges::input_range Types>
    urd::index<Schema> intern_schema(const Types& types)
    {
        auto record = schema_builders.make_unique();
        urd::clear_fields(*record);
        for (const TypeId type : types)
            record->column_types.push_back(type);
        return relations.schema(*record);
    }
};

template<typename SymbolRepo, typename RelationRepo>
Repository<SymbolRepo, RelationRepo>::Repository(const Repository* parent) :
    m_parent(parent),
    m_layer(next_layer()),
    m_impl(std::make_unique<Impl>(*this, parent, default_object_index_width))
{
}

template<typename SymbolRepo, typename RelationRepo>
Repository<SymbolRepo, RelationRepo>::Repository(std::size_t num_objects, const Repository* parent) :
    m_parent(parent),
    m_layer(next_layer()),
    m_impl(std::make_unique<Impl>(*this, parent, Impl::object_index_width(num_objects, parent)))
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
{ return urd::contains_key(m_impl->symbols, index); }

template<typename SymbolRepo, typename RelationRepo>
template<typename T>
std::size_t Repository<SymbolRepo, RelationRepo>::do_size() const
{ return m_impl->symbols.template size<T>(); }

template<typename SymbolRepo, typename RelationRepo>
template<typename T>
std::size_t Repository<SymbolRepo, RelationRepo>::do_own_size() const
{ return m_impl->symbols.template own_size<T>(); }

template<typename SymbolRepo, typename RelationRepo>
template<typename T>
std::size_t Repository<SymbolRepo, RelationRepo>::do_memory_usage() const
{ return m_impl->symbols.template memory_usage<T>(); }

template<typename SymbolRepo, typename RelationRepo>
template<typename T>
const Repository<SymbolRepo, RelationRepo>* Repository<SymbolRepo, RelationRepo>::do_owner(urd::index<T> index) const
{
    if (!urd::contains_key(m_impl->symbols, index))
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
const Repository<SymbolRepo, RelationRepo>* Repository<SymbolRepo, RelationRepo>::do_owner(urd::key<F, Row<F>> key) const
{
    if (key.template get<F>().is_none())
        return nullptr;
    // One walk up the layers: the nearest own pool bounds the rows; the first whose own rows start at or below the row
    // holds it (a pool layers over the nearest ancestor's).
    auto bounded = false;
    for (const auto* layer = this; layer != nullptr; layer = layer->m_parent)
    {
        const auto* rows = layer->m_impl->relations.template pools<F>().own_value(key.template get<F>());
        if (rows == nullptr)
            continue;
        if (!bounded && (key.template get<Row<F>>().value) >= rows->size())
            return nullptr;
        bounded = true;
        if ((key.template get<Row<F>>().value) >= rows->base())
            return layer;
    }
    return nullptr;
}

template<typename SymbolRepo, typename RelationRepo>
template<typename F>
std::span<const TypeId> Repository<SymbolRepo, RelationRepo>::do_types(urd::index<F> relation) const
{
    const auto& types = this->at(relation)->column_types;  // its relation symbol's own record (std::out_of_range where no layer holds it)
    return { types.data(), types.size() };
}

template<typename SymbolRepo, typename RelationRepo>
typename Repository<SymbolRepo, RelationRepo>::OwnParts Repository<SymbolRepo, RelationRepo>::own_parts() const
{
    return OwnParts(std::tuple_element_t<0, OwnParts>(*this, m_impl->symbols), std::tuple_element_t<1, OwnParts>(*this, m_impl->relations));
}

template<typename SymbolRepo, typename RelationRepo>
typename Repository<SymbolRepo, RelationRepo>::OwnWritableParts Repository<SymbolRepo, RelationRepo>::own_writable_parts()
{
    return OwnWritableParts(std::tuple_element_t<0, OwnWritableParts>(*this, m_impl->symbols), std::tuple_element_t<1, OwnWritableParts>(*this, m_impl->slots));
}

template<typename SymbolRepo, typename RelationRepo>
template<typename F>
    requires HasRows<Repository<SymbolRepo, RelationRepo>, F>
detail::RowSlot<F, typename detail::relation_repository_traits<RelationRepo>::Store::template Rows<F>>
Repository<SymbolRepo, RelationRepo>::RelationSlots::own(urd::index<F> relation)
{
    return repository->m_impl->relations.own(relation, repository->types(relation));
}

template<typename SymbolRepo, typename RelationRepo>
template<typename T>
    requires boost::mp11::mp_contains<typename detail::relation_repository_traits<RelationRepo>::Store::RecordTypes, T>::value
typename detail::relation_repository_traits<RelationRepo>::Store::Schemas& Repository<SymbolRepo, RelationRepo>::RelationSlots::get()
{ return repository->m_impl->relations.template get<T>(); }

template<typename SymbolRepo, typename RelationRepo>
template<typename T>
urd::index_view<T, Repository<SymbolRepo, RelationRepo>> Repository<SymbolRepo, RelationRepo>::do_view(urd::index<T> index) const
{
    if (const auto* layer = owner(index))
        return { index, layer };
    throw std::out_of_range("Symbol index not found in any repository layer.");
}

template<typename SymbolRepo, typename RelationRepo>
urd::index_view<Schema, Repository<SymbolRepo, RelationRepo>> Repository<SymbolRepo, RelationRepo>::view(urd::index<Schema> index) const
{
    if (!urd::contains_key(m_impl->relations, index))
        throw std::out_of_range("Schema index not found in any repository layer.");
    const auto* store = &m_impl->relations.template get<Schema>().owner(index);
    const auto* layer = this;
    while (&layer->m_impl->relations.template get<Schema>() != store)
        layer = layer->m_parent;
    return { index, layer };
}

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
RowView<F, Repository<SymbolRepo, RelationRepo>> Repository<SymbolRepo, RelationRepo>::do_view(urd::key<F, Row<F>> key) const
{
    if (const auto* layer = owner(key))
        return { key, layer };
    throw std::out_of_range("Row index not found in any repository layer.");
}

template<typename SymbolRepo, typename RelationRepo>
urd::index<Schema> Repository<SymbolRepo, RelationRepo>::do_schema(std::span<const urd::data<ColumnLayout>> columns)
{
    return m_impl->intern_schema(columns | std::views::transform(&urd::data<ColumnLayout>::type));
}

template<typename SymbolRepo, typename RelationRepo>
urd::index<Schema> Repository<SymbolRepo, RelationRepo>::do_schema(std::span<const TypeId> types)
{
    return m_impl->intern_schema(types);
}

}  // namespace ygg::formalism

#endif
