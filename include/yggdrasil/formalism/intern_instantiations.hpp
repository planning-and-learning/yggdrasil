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

#ifndef YGG_FORMALISM_INTERN_INSTANTIATIONS_HPP_
#define YGG_FORMALISM_INTERN_INSTANTIATIONS_HPP_

// Explicit instantiations of intern for every symbol type of a symbol repository, from its type list (docs/
// TEMPLATE_RULES.md, rules 7 and 9): a library instantiates them (YGG_FORMALISM_INTERNS(, Count, ThreadSafe, Types)),
// and the TUs that intern every type declare them (YGG_FORMALISM_INTERNS(extern, ...)) through the library's opt-in
// detail/<name>_instantiations.hpp, so they do not instantiate the interning chain themselves. Only the list and its
// types' declarations are needed: no repository or store class is instantiated.

#include "yggdrasil/formalism/symbol_repository.hpp"

#include <boost/mp11.hpp>
#include <boost/preprocessor/punctuation/remove_parens.hpp>
#include <boost/preprocessor/repetition/repeat.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <cstddef>

namespace ygg::formalism::detail
{

/// The I-th type of a list of symbol types, and the store a repository (thread-safe or not) interns it in.
template<typename Types, std::size_t I>
using InternType = boost::mp11::mp_at_c<Types, I>;
template<typename Types, bool ThreadSafe, std::size_t I>
using InternStore = SymbolStore<InternType<Types, I>, ThreadSafe>;

}  // namespace ygg::formalism::detail

/// The list of YGG_FORMALISM_INTERN_AT's arguments (PREFIX, ThreadSafe, (Types)).
#define YGG_FORMALISM_INTERN_TYPES(ARGS) BOOST_PP_REMOVE_PARENS(BOOST_PP_TUPLE_ELEM(3, 2, ARGS))
/// The explicit instantiation (PREFIX extern: its declaration) of intern for the I-th type of the list.
#define YGG_FORMALISM_INTERN_AT(z, I, ARGS)                                                                                     \
    BOOST_PP_TUPLE_ELEM(3, 0, ARGS) template std::pair<urd::index<::ygg::formalism::detail::InternType<YGG_FORMALISM_INTERN_TYPES(ARGS), I>>, bool> \
    ygg::formalism::intern(::ygg::formalism::detail::InternStore<YGG_FORMALISM_INTERN_TYPES(ARGS), BOOST_PP_TUPLE_ELEM(3, 1, ARGS), I>&,          \
                           urd::data<::ygg::formalism::detail::InternType<YGG_FORMALISM_INTERN_TYPES(ARGS), I>>&);

/// Explicit instantiations (PREFIX extern: declarations) of intern for each of the Count symbol types of the list given
/// last (a repository's SymbolTypes, or several lists' boost::mp11::mp_unique<boost::mp11::mp_append<...>>: a
/// specialization is explicitly instantiated once), interned in stores ThreadSafe or not (the repository's
/// thread_safe). Count is checked against the list.
#define YGG_FORMALISM_INTERNS(PREFIX, Count, ThreadSafe, ...)                                         \
    static_assert(boost::mp11::mp_size<__VA_ARGS__>::value == (Count),                                 \
                  "YGG_FORMALISM_INTERNS: Count must be the number of the list's symbol types");     \
    BOOST_PP_REPEAT(Count, YGG_FORMALISM_INTERN_AT, (PREFIX, ThreadSafe, (__VA_ARGS__)))

#endif
