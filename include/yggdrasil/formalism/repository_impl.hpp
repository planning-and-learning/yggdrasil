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
#include <urd/store.hpp>
#include <utility>

namespace ygg::formalism
{

template<NamesList SymbolRepo, NamesList RelationRepo>
struct Repository<SymbolRepo, RelationRepo>::Impl
{
    /// A pool key's records stay across clear(), and so do their pools.
    template<typename T>
    using KeysPools = boost::mp11::mp_contains<RelationTypes, T>;

    /// The stores of what the repository holds (declarations.hpp, Symbols and Relations).
    using SymbolStore = typename detail::symbol_repository_traits<SymbolRepo>::Store;
    using RelationStore = typename detail::relation_repository_traits<RelationRepo>::Store;

    SymbolStore symbols;
    /// Its relations, which read a relation's column types from its symbols (when they make the relation's pool).
    RelationStore relations;

    Impl(const Repository& repository, const Repository* parent) :
        symbols(parent ? &parent->m_impl->symbols : nullptr),
        relations(parent ? &parent->m_impl->relations : nullptr, column_widths(repository))
    {
    }

    /// The column widths of relation `relation`: of the column types its view in `repository` gives (its record's,
    /// stored or computed); std::out_of_range where no layer holds it.
    template<typename F>
    static urd::column_widths read_column_widths(const void* repository, urd::index<F> relation)
    { return RelationStore::widths(static_cast<const Repository*>(repository)->view(relation).column_types()); }

    /// The reader of family F's column widths from `owner`, the repository these stores belong to: where it holds F's
    /// relation records; else none, so that a pool of F is never made (std::logic_error).
    template<typename F>
    static detail::ColumnWidthsReader<F> column_widths_of(const Repository& owner)
    {
        if constexpr (HoldsRecords<Repository, F>)
            return { &owner, &read_column_widths<F> };
        else
            return {};
    }

    /// The readers of every relation family's column widths from `repository`.
    static typename RelationStore::ColumnWidths column_widths(const Repository& repository)
    {
        return []<typename... Fs>(boost::mp11::mp_list<Fs...> /*families*/, const Repository& owner)
        { return typename RelationStore::ColumnWidths(column_widths_of<Fs>(owner)...); }(RelationTypes {}, repository);
    }
};

template<NamesList SymbolRepo, NamesList RelationRepo>
Repository<SymbolRepo, RelationRepo>::Repository(const Repository* parent) :
    m_parent(parent),
    m_layer(next_layer()),
    m_impl(std::make_unique<Impl>(*this, parent))
{
}

template<NamesList SymbolRepo, NamesList RelationRepo>
Repository<SymbolRepo, RelationRepo>::~Repository()  // NOLINT(modernize-use-equals-default): GCC rejects an explicit instantiation of an out-of-line = default
{
}

template<NamesList SymbolRepo, NamesList RelationRepo>
void Repository<SymbolRepo, RelationRepo>::clear()
{
    boost::mp11::mp_for_each<boost::mp11::mp_transform<std::type_identity, boost::mp11::mp_remove_if<SymbolTypes, Impl::template KeysPools>>>(
        [this]<typename T>(std::type_identity<T>) { m_impl->symbols.template get<T>().clear(); });
    m_impl->relations.clear();
}

template<NamesList SymbolRepo, NamesList RelationRepo>
void Repository<SymbolRepo, RelationRepo>::clear_all()
{
    m_impl->symbols.clear();
    m_impl->relations.clear();
}

template<NamesList SymbolRepo, NamesList RelationRepo>
void Repository<SymbolRepo, RelationRepo>::set_one_writer(bool one_writer) noexcept
{ m_impl->relations.set_one_writer(one_writer); }

template<NamesList SymbolRepo, NamesList RelationRepo>
typename Repository<SymbolRepo, RelationRepo>::template OwnParts<false> Repository<SymbolRepo, RelationRepo>::own_parts() const
{
    using Parts = OwnParts<false>;
    return Parts(std::tuple_element_t<0, Parts>(*this, m_impl->symbols), std::tuple_element_t<1, Parts>(*this, m_impl->relations));
}

template<NamesList SymbolRepo, NamesList RelationRepo>
typename Repository<SymbolRepo, RelationRepo>::template OwnParts<true> Repository<SymbolRepo, RelationRepo>::own_parts()
{
    using Parts = OwnParts<true>;
    return Parts(std::tuple_element_t<0, Parts>(*this, m_impl->symbols), std::tuple_element_t<1, Parts>(*this, m_impl->relations));
}

}  // namespace ygg::formalism

#endif
