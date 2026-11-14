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

#ifndef YGG_FORMALISM_SYMBOL_CONTEXT_INSTANTIATIONS_HPP_
#define YGG_FORMALISM_SYMBOL_CONTEXT_INSTANTIATIONS_HPP_

// Explicit instantiations of the members of a repository's SymbolContext base (symbol_context.hpp) for each of its
// symbol types (rule 7 of the template rules, https://planning-and-learning.github.io/brokkr/template_rules.html): the
// one source of a library that includes symbol_context_impl.hpp instantiates each of its repositories,
//
//     YGG_FORMALISM_SYMBOL_CONTEXT(, Repository, SymbolCount)
//
// and its other sources include symbol_context.hpp only and link to these.

#include "yggdrasil/formalism/symbol_context_impl.hpp"

#include <boost/mp11.hpp>
#include <boost/preprocessor/repetition/repeat.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <cstddef>

namespace ygg::formalism::detail
{
/// The SymbolContext a repository derives from, and its I-th symbol type.
template<typename Derived, typename Types, bool ThreadSafe>
SymbolContext<Derived, Types, ThreadSafe> symbol_context_of(const SymbolContext<Derived, Types, ThreadSafe>* context);
template<typename Derived>
using SymbolContextOf = decltype(symbol_context_of(static_cast<const Derived*>(nullptr)));
template<typename Derived, std::size_t I>
using ContextSymbol = boost::mp11::mp_at_c<typename SymbolContextOf<Derived>::SymbolTypes, I>;
}  // namespace ygg::formalism::detail

/// The members of the I-th symbol type of the repository (ARGS: (PREFIX, Repository)).
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): it expands to explicit instantiations, declarations, which no function gives
#define YGG_FORMALISM_SYMBOL_CONTEXT_AT(z, I, ARGS)                                                                                            \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS) template std::optional<                                                                                    \
        urd::index_view<::ygg::formalism::detail::ContextSymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>, BOOST_PP_TUPLE_ELEM(2, 1, ARGS)>>         \
    ygg::formalism::detail::SymbolContextOf<BOOST_PP_TUPLE_ELEM(2, 1, ARGS)>::find(                                                          \
        const urd::data<::ygg::formalism::detail::ContextSymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>&) const;                                  \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS) template std::pair<                                                                                        \
        urd::index_view<::ygg::formalism::detail::ContextSymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>, BOOST_PP_TUPLE_ELEM(2, 1, ARGS)>,         \
        bool>                                                                                                                                  \
    ygg::formalism::detail::SymbolContextOf<BOOST_PP_TUPLE_ELEM(2, 1, ARGS)>::insert(                                                        \
        urd::data<::ygg::formalism::detail::ContextSymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>&);                                              \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS) template const urd::data<::ygg::formalism::detail::ContextSymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>&     \
    ygg::formalism::detail::SymbolContextOf<BOOST_PP_TUPLE_ELEM(2, 1, ARGS)>::operator[](                                                    \
        urd::index<::ygg::formalism::detail::ContextSymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>) const;                                        \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS) template bool ygg::formalism::detail::SymbolContextOf<BOOST_PP_TUPLE_ELEM(2, 1, ARGS)>::contains(        \
        urd::index<::ygg::formalism::detail::ContextSymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>) const;                                        \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS) template std::size_t ygg::formalism::detail::SymbolContextOf<BOOST_PP_TUPLE_ELEM(2, 1, ARGS)>::size<     \
        ::ygg::formalism::detail::ContextSymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>() const;                                                  \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS) template std::size_t                                                                                       \
    ygg::formalism::detail::SymbolContextOf<BOOST_PP_TUPLE_ELEM(2, 1, ARGS)>::memory_usage<                                                  \
        ::ygg::formalism::detail::ContextSymbol<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>() const;

/// Explicit instantiations (PREFIX extern: declarations) of the members of repository Repository's SymbolContext base
/// (Repository a name without commas: an alias) that touch its symbols: construction, destruction, clear, and those
/// of each of its SymbolCount symbol types. The count is checked against its list.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): it expands to explicit instantiations, declarations, which no function gives
#define YGG_FORMALISM_SYMBOL_CONTEXT(PREFIX, Repository, SymbolCount)                                                                         \
    static_assert(boost::mp11::mp_size<::ygg::formalism::detail::SymbolContextOf<Repository>::SymbolTypes>::value == (SymbolCount),          \
                  "YGG_FORMALISM_SYMBOL_CONTEXT: SymbolCount");                                                                                \
    PREFIX template ygg::formalism::detail::SymbolContextOf<Repository>::SymbolContext();                                                    \
    PREFIX template ygg::formalism::detail::SymbolContextOf<Repository>::SymbolContext(const Repository*);                                   \
    PREFIX template ygg::formalism::detail::SymbolContextOf<Repository>::~SymbolContext();                                                   \
    PREFIX template void ygg::formalism::detail::SymbolContextOf<Repository>::clear();                                                       \
    BOOST_PP_REPEAT(SymbolCount, YGG_FORMALISM_SYMBOL_CONTEXT_AT, (PREFIX, Repository))

#endif
