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
// the template rules, https://planning-and-learning.github.io/brokkr/template_rules.html): the one source of a library
// that includes repository_impl.hpp (and its domain's prepare_for_insert, which every insert runs) instantiates each of
// its repositories,
//
//     YGG_FORMALISM_REPOSITORY(, Repository, SymbolCount, RowCount)
//
// and its other sources include repository.hpp only and link to these: no other source sees the definitions of its
// inserts (keyed_impl.hpp), so none instantiates them without the domain's preparation.

#include "yggdrasil/formalism/part_instantiations.hpp"
#include "yggdrasil/formalism/repository.hpp"

#include <boost/mp11.hpp>
#include <boost/preprocessor/arithmetic/inc.hpp>
#include <cstddef>

/// Explicit instantiations (PREFIX extern: declarations) of the members of repository Repo (a name without commas: an
/// alias) that touch its stores: construction, destruction and clear, its own parts, and the members of each part for
/// each of its keys (YGG_FORMALISM_PART): its SymbolCount symbols' and its relations' (a Schema's and its RowCount row
/// families'). The counts are checked against its lists.
// NOLINTBEGIN(cppcoreguidelines-macro-usage,bugprone-macro-parentheses): the macro expands to explicit instantiations,
// declarations, which no function gives; its arguments are types and a prefix, which no parentheses may enclose
#define YGG_FORMALISM_REPOSITORY(PREFIX, Repo, SymbolCount, RowCount)                                                    \
    static_assert(boost::mp11::mp_size<Repo::SymbolTypes>::value == (SymbolCount), "YGG_FORMALISM_REPOSITORY: SymbolCount"); \
    static_assert(boost::mp11::mp_size<Repo::RelationTypes>::value == (RowCount), "YGG_FORMALISM_REPOSITORY: RowCount");     \
    PREFIX template Repo::Repository(const Repo*);                                                                       \
    PREFIX template Repo::~Repository();                                                                                 \
    PREFIX template void Repo::clear();                                                                                  \
    PREFIX template void Repo::clear_all();                                                                              \
    PREFIX template Repo::OwnParts<false> Repo::own_parts() const;                                                       \
    PREFIX template Repo::OwnParts<true> Repo::own_parts();                                                              \
    YGG_FORMALISM_PART(PREFIX, Repo, 0, SymbolCount)                                                                     \
    YGG_FORMALISM_PART(PREFIX, Repo, 1, BOOST_PP_INC(RowCount))
// NOLINTEND(cppcoreguidelines-macro-usage,bugprone-macro-parentheses)

#endif
