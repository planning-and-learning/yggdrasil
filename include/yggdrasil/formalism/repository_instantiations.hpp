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
//     YGG_FORMALISM_REPOSITORY(, Repository, SymbolCount, RowCount, RelationCount)
//
// Its other sources include repository.hpp only and link to these.

#include "yggdrasil/formalism/repository_impl.hpp"

#include <boost/mp11.hpp>
#include <boost/preprocessor/repetition/repeat.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <cstddef>

namespace ygg::formalism::detail
{
/// The I-th symbol type, row family and family with relations of a repository.
template<typename Repo, std::size_t I>
using RepositorySymbol = boost::mp11::mp_at_c<typename Repo::SymbolTypes, I>;
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
    template std::optional<urd::index_view<::ygg::formalism::detail::RepositorySymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>, BOOST_PP_TUPLE_ELEM(2, 1, ARGS)>>   \
        BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::do_find(const urd::data<::ygg::formalism::detail::RepositorySymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>&) const;      \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                            \
    template std::pair<urd::index_view<::ygg::formalism::detail::RepositorySymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>, BOOST_PP_TUPLE_ELEM(2, 1, ARGS)>, bool> \
        BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::do_insert(urd::data<::ygg::formalism::detail::RepositorySymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>&);                \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                            \
    template const urd::data<::ygg::formalism::detail::RepositorySymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>& BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::do_at(          \
        urd::index<::ygg::formalism::detail::RepositorySymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>) const;                                                     \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                            \
    template urd::index_view<::ygg::formalism::detail::RepositorySymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>, BOOST_PP_TUPLE_ELEM(2, 1, ARGS)>                  \
        BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::do_view(urd::index<::ygg::formalism::detail::RepositorySymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>) const;            \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                            \
    template bool BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::do_contains(urd::index<::ygg::formalism::detail::RepositorySymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>)     \
        const;                                                                                                                                                 \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                            \
    template std::size_t BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::do_size<::ygg::formalism::detail::RepositorySymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>() const;     \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                            \
    template std::size_t BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::do_memory_usage<::ygg::formalism::detail::RepositorySymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>()    \
        const;

/// The members of the I-th row family F of the repository (ARGS: (PREFIX, Repo)).
#define YGG_FORMALISM_REPOSITORY_ROWS(z, I, ARGS)                                                                                                             \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                           \
    template const BOOST_PP_TUPLE_ELEM(2, 1, ARGS)                                                                                                            \
        * BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::do_owner(::ygg::RowIndex<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>) const;     \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                           \
    template std::optional<::ygg::RowView<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>, BOOST_PP_TUPLE_ELEM(2, 1, ARGS)>>     \
        BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::do_find(const urd::data<::ygg::Row<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>>&)  \
            const;                                                                                                                                            \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                           \
    template std::pair<::ygg::RowView<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>, BOOST_PP_TUPLE_ELEM(2, 1, ARGS)>, bool>   \
        BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::do_insert(                                                                                                           \
            const urd::data<::ygg::Row<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>>&);                                      \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                           \
    template urd::index<::ygg::Row<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>> BOOST_PP_TUPLE_ELEM(                        \
        2,                                                                                                                                                    \
        1,                                                                                                                                                    \
        ARGS)::do_find_row(urd::index<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>, std::span<const ::ygg::Value>) const;    \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                           \
    template urd::index<::ygg::Row<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>> BOOST_PP_TUPLE_ELEM(                        \
        2,                                                                                                                                                    \
        1,                                                                                                                                                    \
        ARGS)::do_find_row(urd::index<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>,                                          \
                           const BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::StoredRow&) const;                                                                          \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                           \
    template std::size_t BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::do_size(urd::index<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>)   \
        const;                                                                                                                                                \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                           \
    template std::size_t BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::do_row_memory_usage<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>() \
        const;                                                                                                                                                \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                           \
    template std::pair<urd::index<::ygg::Row<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>>, bool> BOOST_PP_TUPLE_ELEM(       \
        2,                                                                                                                                                    \
        1,                                                                                                                                                    \
        ARGS)::do_insert_row(const urd::data<::ygg::Row<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>>&);                     \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                           \
    template BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::StoredRow BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::do_at(                                                               \
        ::ygg::RowIndex<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>) const;                                                 \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                           \
    template ::ygg::RowView<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>, BOOST_PP_TUPLE_ELEM(2, 1, ARGS)>                    \
        BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::do_view(::ygg::RowIndex<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>) const;        \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                           \
    template std::span<const ::ygg::TypeId> BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::do_types(                                                                        \
        urd::index<::ygg::formalism::detail::RepositoryRows<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>) const;

/// The members of the I-th family F with relations of the repository (ARGS: (PREFIX, Repo)); its insert of a relation is
/// the symbol type Relation<F>'s (YGG_FORMALISM_REPOSITORY_SYMBOL).
#define YGG_FORMALISM_REPOSITORY_RELATIONS(z, I, ARGS)                                                                                         \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                            \
    template std::shared_ptr<urd::builder<::ygg::Relation<::ygg::formalism::detail::RepositoryRelations<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>>> \
        BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::do_get_relation_builder(std::span<const ::ygg::ColumnLayout>) const;

/// Explicit instantiations (PREFIX extern: declarations) of the members of repository Repo (a name without commas: an
/// alias) that touch its stores: construction, destruction and clear, and those of each of its SymbolCount symbol types,
/// RowCount row families and RelationCount families with relations. The counts are checked against its lists. schema() of a repository
/// with free relations: YGG_FORMALISM_REPOSITORY_SCHEMA.
#define YGG_FORMALISM_REPOSITORY(PREFIX, Repo, SymbolCount, RowCount, RelationCount)                                                        \
    static_assert(boost::mp11::mp_size<Repo::SymbolTypes>::value == (SymbolCount), "YGG_FORMALISM_REPOSITORY: SymbolCount");                \
    static_assert(boost::mp11::mp_size<Repo::RelationTypes>::value == (RowCount), "YGG_FORMALISM_REPOSITORY: RowCount");                    \
    static_assert(boost::mp11::mp_size<Repo::Impl::RelationFamilies>::value == (RelationCount), "YGG_FORMALISM_REPOSITORY: RelationCount"); \
    PREFIX template Repo::Repository(const Repo*);                                                                                          \
    PREFIX template Repo::Repository(std::size_t, const Repo*);                                                                             \
    PREFIX template Repo::~Repository();                                                                                                    \
    PREFIX template void Repo::clear();                                                                                                     \
    PREFIX template void Repo::clear_all();                                                                                                 \
    PREFIX template urd::index_view<::ygg::Schema, Repo> Repo::view(urd::index<::ygg::Schema>) const;                                       \
    BOOST_PP_REPEAT(SymbolCount, YGG_FORMALISM_REPOSITORY_SYMBOL, (PREFIX, Repo))                                                           \
    BOOST_PP_REPEAT(RowCount, YGG_FORMALISM_REPOSITORY_ROWS, (PREFIX, Repo))                                                                \
    BOOST_PP_REPEAT(RelationCount, YGG_FORMALISM_REPOSITORY_RELATIONS, (PREFIX, Repo))

/// The explicit instantiation (PREFIX extern: its declaration) of schema() of repository Repo, which has free relations
/// (rows of Schema).
#define YGG_FORMALISM_REPOSITORY_SCHEMA(PREFIX, Repo)                                          \
    PREFIX template urd::index<::ygg::Schema> Repo::do_schema(std::span<const ::ygg::TypeId>); \
    PREFIX template urd::index<::ygg::Schema> Repo::do_schema(std::span<const ::ygg::ColumnLayout>);

// NOLINTEND(cppcoreguidelines-macro-usage,bugprone-macro-parentheses)

#endif
