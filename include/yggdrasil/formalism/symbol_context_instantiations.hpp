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
// one source of a library that includes symbol_context_impl.hpp (this header does not: a header may declare the
// instantiations, PREFIX extern, without seeing the definitions) instantiates each of its repositories,
//
//     YGG_FORMALISM_SYMBOL_CONTEXT(, Repository, SymbolCount)
//
// and its other sources include symbol_context.hpp only and link to these.

#include "yggdrasil/formalism/part_instantiations.hpp"
#include "yggdrasil/formalism/symbol_context.hpp"

#include <boost/mp11.hpp>
#include <boost/preprocessor/repetition/repeat.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <cstddef>

namespace ygg::formalism::detail
{
/// The SymbolContext a repository derives from.
template<typename Derived, typename Types, bool ThreadSafe>
SymbolContext<Derived, Types, ThreadSafe> symbol_context_of(const SymbolContext<Derived, Types, ThreadSafe>* context);
template<typename Derived>
using SymbolContextOf = decltype(symbol_context_of(static_cast<const Derived*>(nullptr)));
}  // namespace ygg::formalism::detail

// NOLINTBEGIN(cppcoreguidelines-macro-usage,bugprone-macro-parentheses): the macro expands to explicit instantiations,
// declarations, which no function gives; its arguments are types and a prefix, which no parentheses may enclose
/// Explicit instantiations (PREFIX extern: declarations) of the members of repository Repository's SymbolContext base
/// (Repository a name without commas: an alias) that touch its symbols: construction, destruction, clear, its own part, and those
/// of each of its SymbolCount symbol types. The count is checked against its list.
#define YGG_FORMALISM_SYMBOL_CONTEXT(PREFIX, Repository, SymbolCount)                                                               \
    static_assert(boost::mp11::mp_size<::ygg::formalism::detail::SymbolContextOf<Repository>::SymbolTypes>::value == (SymbolCount), \
                  "YGG_FORMALISM_SYMBOL_CONTEXT: SymbolCount");                                                                     \
    YGG_FORMALISM_SYMBOL_CONTEXT_MEMBERS(PREFIX, Repository)                                                                        \
    YGG_FORMALISM_PART(PREFIX, Repository, 0, SymbolCount)

/// The same in parts, for a repository of many symbols split over several sources (brokkr's template rule T5): its
/// construction, destruction, clear and own parts here, its symbols' keys with YGG_FORMALISM_PART_KEYS for part 0.
#define YGG_FORMALISM_SYMBOL_CONTEXT_MEMBERS(PREFIX, Repository)                                                                                          \
    PREFIX template ygg::formalism::detail::SymbolContextOf<Repository>::SymbolContext();                                                                 \
    PREFIX template ygg::formalism::detail::SymbolContextOf<Repository>::SymbolContext(const Repository*);                                                \
    PREFIX template ygg::formalism::detail::SymbolContextOf<Repository>::~SymbolContext();                                                                \
    PREFIX template void ygg::formalism::detail::SymbolContextOf<Repository>::clear();                                                                    \
    PREFIX template ygg::formalism::detail::SymbolContextOf<Repository>::OwnParts<false> ygg::formalism::detail::SymbolContextOf<Repository>::own_parts() \
        const;                                                                                                                                            \
    PREFIX template ygg::formalism::detail::SymbolContextOf<Repository>::OwnParts<true> ygg::formalism::detail::SymbolContextOf<Repository>::own_parts();

// NOLINTEND(cppcoreguidelines-macro-usage,bugprone-macro-parentheses)

#endif
