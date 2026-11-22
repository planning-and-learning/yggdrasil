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

#ifndef YGG_FORMALISM_REPOSITORY_INSTANTIATIONS_HPP_
#define YGG_FORMALISM_REPOSITORY_INSTANTIATIONS_HPP_

// Explicit instantiations of a Repository and of its members for each of its symbol types and row families (rule 7 of
// the template rules, https://planning-and-learning.github.io/brokkr/template_rules.html): repository.hpp declares
// them, and the one source of a library that includes repository_impl.hpp instantiates each of its repositories:
//
//     YGG_FORMALISM_REPOSITORY(, Repository, SymbolCount, RowCount)
//
// Its other sources include repository.hpp only and link to these.

#include "yggdrasil/formalism/repository_impl.hpp"

#include <boost/mp11.hpp>
#include <boost/preprocessor/repetition/repeat.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <cstddef>
#include <tuple>

namespace ygg::formalism::detail
{
/// The I-th symbol type, row family and family with relations of a repository.
template<typename Repo, std::size_t I>
using RepositorySymbol = boost::mp11::mp_at_c<typename Repo::SymbolTypes, I>;
/// The I-th own part of the repository (Repo::OwnParts): its symbols (0), its relations (1).
template<typename Repo, std::size_t I>
using RepositoryPart = std::tuple_element_t<I, typename Repo::OwnParts>;
/// The I-th own part the repository is written through (Repo::OwnWritableParts), likewise.
template<typename Repo, std::size_t I>
using RepositoryWriter = std::tuple_element_t<I, typename Repo::OwnWritableParts>;
template<typename Repo, std::size_t I>
using RepositoryRows = boost::mp11::mp_at_c<typename Repo::RelationTypes, I>;
template<typename Repo, std::size_t I>
using RepositoryRelations = boost::mp11::mp_at_c<typename Repo::Impl::RelationFamilies, I>;
}  // namespace ygg::formalism::detail

/// The members of the I-th symbol type T of the repository (ARGS: (PREFIX, Repo)).
// NOLINTBEGIN(cppcoreguidelines-macro-usage,bugprone-macro-parentheses): the macros expand to explicit instantiations,
// declarations, which no function gives; their arguments are types and a prefix, which no parentheses may enclose
#define YGG_FORMALISM_REPOSITORY_SYMBOL(z, I, ARGS)                                                                                                            \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                            \
    template const BOOST_PP_TUPLE_ELEM(2, 1, ARGS)                                                                                                             \
        * BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::do_owner(urd::index<::ygg::formalism::detail::RepositorySymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>) const;         \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                            \
    template std::optional<urd::index_view<::ygg::formalism::detail::RepositorySymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>, BOOST_PP_TUPLE_ELEM(2, 1, ARGS), ::urd::key<::ygg::formalism::detail::RepositorySymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>>> ::ygg::formalism::detail::RepositoryPart<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), 0>::find(const urd::data<::ygg::formalism::detail::RepositorySymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>&) const;\
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                         \
    template std::pair<urd::index_view<::ygg::formalism::detail::RepositorySymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>, BOOST_PP_TUPLE_ELEM(2, 1, ARGS), ::urd::key<::ygg::formalism::detail::RepositorySymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>>, bool> ::ygg::formalism::detail::RepositoryWriter<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), 0>::insert(urd::data<::ygg::formalism::detail::RepositorySymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>&) const;\
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                            \
    template urd::stored_ref<::ygg::formalism::detail::RepositorySymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>                                                         \
        ::ygg::formalism::detail::RepositoryPart<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), 0>::operator[](                                                              \
            urd::index<::ygg::formalism::detail::RepositorySymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>) const;                                                 \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                            \
    template urd::stored_ref<::ygg::formalism::detail::RepositorySymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>                                                         \
        ::ygg::formalism::detail::RepositoryPart<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), 0>::at(                                                                      \
            urd::index<::ygg::formalism::detail::RepositorySymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>) const;                                                 \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                            \
    template urd::index_view<::ygg::formalism::detail::RepositorySymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>, BOOST_PP_TUPLE_ELEM(2, 1, ARGS)>                  \
        BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::do_view(urd::index<::ygg::formalism::detail::RepositorySymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>) const;            \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                            \
    template bool BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::do_contains(urd::index<::ygg::formalism::detail::RepositorySymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>)     \
        const;                                                                                                                                                 \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                            \
    template std::size_t BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::do_size<::ygg::formalism::detail::RepositorySymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>() const;     \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                            \
    template std::size_t BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::do_own_size<::ygg::formalism::detail::RepositorySymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>() const; \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                            \
    template std::size_t BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::do_memory_usage<::ygg::formalism::detail::RepositorySymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>()    \
        const;

/// The members of the I-th row family F of the repository (ARGS: (PREFIX, Repo)).
#define YGG_FORMALISM_REPOSITORY_ROWS(z, I, ARGS)                                                                                                                                                                                                                                                                                                                                                                                                                  \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                                                                                                                                                                                                                                                                                                                                \
    template const BOOST_PP_TUPLE_ELEM(2, 1, ARGS)                                                                                                                                                                                                                                                                                                                                                                                                                 \
        * BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::do_owner(::urd::key<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>, ::ygg::Row<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>>) const;                                                                                                                                                                                                                     \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                                                                                                                                                                                                                                                                                                                                \
    template std::optional<::ygg::RowView<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>, BOOST_PP_TUPLE_ELEM(2, 1, ARGS)>> ::ygg::formalism::detail::RepositoryPart<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), 1>::find(const urd::data<::ygg::Row<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>>&) const;                                                 \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                                                                                                                                                                                                                                                                                                                                \
    template std::pair<::ygg::RowView<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>, BOOST_PP_TUPLE_ELEM(2, 1, ARGS)>, bool> ::ygg::formalism::detail::RepositoryWriter<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), 1>::insert(urd::data<::ygg::Row<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>>&) const;                                                 \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                                                                                                                                                                                                                                                                                                                                \
    template std::size_t BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::do_size(urd::index<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>) const;                                                                                                                                                                                                                                                                                                 \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                                                                                                                                                                                                                                                                                                                                \
    template std::size_t BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::do_row_memory_usage<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>() const;                                                                                                                                                                                                                                                                                               \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                                                                                                                                                                                                                                                                                                                                \
    template ::ygg::RowView<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>, BOOST_PP_TUPLE_ELEM(2, 1, ARGS)> BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::do_view(::urd::key<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>, ::ygg::Row<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>>) const;                                                                                     \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                                                                                                                                                                                                                                                                                                                                \
    template urd::stored_ref<::ygg::Row<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>> ::ygg::formalism::detail::RepositoryPart<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), 1>::operator[](::urd::key<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>, ::ygg::Row<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>>) const;                                                         \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                                                                                                                                                                                                                                                                                                                                \
    template urd::stored_ref<::ygg::Row<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>> ::ygg::formalism::detail::RepositoryPart<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), 1>::at(::urd::key<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>, ::ygg::Row<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>>) const;                                                                 \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                                                                                                                                                                                                                                                                                                                                \
    template std::span<const ::ygg::TypeId> BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::do_types(urd::index<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>) const;

/// Explicit instantiations (PREFIX extern: declarations) of the members of repository Repo (a name without commas: an
/// alias) that touch its stores: construction, destruction and clear, and those of each of its SymbolCount symbol types
/// and RowCount row families. The counts are checked against its lists. schema() of a repository
/// with free relations: YGG_FORMALISM_REPOSITORY_SCHEMA.
#define YGG_FORMALISM_REPOSITORY(PREFIX, Repo, SymbolCount, RowCount)                                                        \
    static_assert(boost::mp11::mp_size<Repo::SymbolTypes>::value == (SymbolCount), "YGG_FORMALISM_REPOSITORY: SymbolCount"); \
    static_assert(boost::mp11::mp_size<Repo::RelationTypes>::value == (RowCount), "YGG_FORMALISM_REPOSITORY: RowCount");     \
    PREFIX template Repo::Repository(const Repo*);                                                                           \
    PREFIX template Repo::Repository(std::size_t, const Repo*);                                                              \
    PREFIX template Repo::~Repository();                                                                                     \
    PREFIX template void Repo::clear();                                                                                      \
    PREFIX template void Repo::clear_all();                                                                                  \
    PREFIX template urd::index_view<::ygg::Schema, Repo> Repo::view(urd::index<::ygg::Schema>) const;                        \
    PREFIX template Repo::OwnParts Repo::own_parts() const;                                                                  \
    PREFIX template Repo::OwnWritableParts Repo::own_writable_parts();                                                        \
    PREFIX template std::optional<urd::index_view<::ygg::Schema, Repo, ::urd::key<::ygg::Schema>>> ::ygg::formalism::detail::RepositoryPart<Repo, 1>::find(const urd::data<::ygg::Schema>&) const; \
    PREFIX template std::pair<urd::index_view<::ygg::Schema, Repo, ::urd::key<::ygg::Schema>>, bool> ::ygg::formalism::detail::RepositoryWriter<Repo, 1>::insert(urd::data<::ygg::Schema>&) const; \
    PREFIX template urd::stored_ref<::ygg::Schema> ::ygg::formalism::detail::RepositoryPart<Repo, 1>::operator[](           \
        urd::index<::ygg::Schema>) const;                                                                                    \
    PREFIX template urd::stored_ref<::ygg::Schema> ::ygg::formalism::detail::RepositoryPart<Repo, 1>::at(                   \
        urd::index<::ygg::Schema>) const;                                                                                    \
    BOOST_PP_REPEAT(SymbolCount, YGG_FORMALISM_REPOSITORY_SYMBOL, (PREFIX, Repo))                                            \
    BOOST_PP_REPEAT(RowCount, YGG_FORMALISM_REPOSITORY_ROWS, (PREFIX, Repo))

/// The explicit instantiation (PREFIX extern: its declaration) of schema() of repository Repo, which has free relations
/// (rows of Schema).
#define YGG_FORMALISM_REPOSITORY_SCHEMA(PREFIX, Repo)                                          \
    PREFIX template urd::index<::ygg::Schema> Repo::do_schema(std::span<const ::ygg::TypeId>); \
    PREFIX template urd::index<::ygg::Schema> Repo::do_schema(std::span<const urd::data<::ygg::ColumnLayout>>);

// NOLINTEND(cppcoreguidelines-macro-usage,bugprone-macro-parentheses)

#endif
