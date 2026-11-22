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

#ifndef YGG_FORMALISM_SYMBOL_CONTEXT_IMPL_HPP_
#define YGG_FORMALISM_SYMBOL_CONTEXT_IMPL_HPP_

// The definitions of SymbolContext (symbol_context.hpp): its stores (Impl, a plain symbol repository) and every member.
// Included by the source that instantiates a library's repositories (symbol_context_instantiations.hpp), and by code
// whose repositories are not a library's (yggdrasil's tests).

#include "yggdrasil/formalism/keyed_impl.hpp"
#include "yggdrasil/formalism/symbol_context.hpp"
#include "yggdrasil/formalism/symbol_repository.hpp"

#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace ygg::formalism
{

template<typename Derived, bool ThreadSafe, typename... Ts>
struct SymbolContext<Derived, boost::mp11::mp_list<Ts...>, ThreadSafe>::Impl
{
    using Symbols = SymbolStores<ThreadSafe, Ts...>;

    Symbols symbols;

    explicit Impl(const Impl* parent) : symbols(parent == nullptr ? nullptr : &parent->symbols) {}
};

template<typename Derived, bool ThreadSafe, typename... Ts>
SymbolContext<Derived, boost::mp11::mp_list<Ts...>, ThreadSafe>::SymbolContext() : m_impl(std::make_unique<Impl>(nullptr))
{
}

template<typename Derived, bool ThreadSafe, typename... Ts>
SymbolContext<Derived, boost::mp11::mp_list<Ts...>, ThreadSafe>::SymbolContext(const Derived* parent) :
    m_parent(parent),
    m_impl(std::make_unique<Impl>(parent == nullptr ? nullptr : static_cast<const SymbolContext*>(parent)->m_impl.get()))
{
}

template<typename Derived, bool ThreadSafe, typename... Ts>
SymbolContext<Derived, boost::mp11::mp_list<Ts...>, ThreadSafe>::~SymbolContext()  // NOLINT(modernize-use-equals-default): GCC rejects an explicit
                                                                                   // instantiation of an out-of-line = default
{
}

template<typename Derived, bool ThreadSafe, typename... Ts>
void SymbolContext<Derived, boost::mp11::mp_list<Ts...>, ThreadSafe>::clear()
{ m_impl->symbols.clear(); }

template<typename Derived, bool ThreadSafe, typename... Ts>
typename SymbolContext<Derived, boost::mp11::mp_list<Ts...>, ThreadSafe>::template OwnParts<false>
SymbolContext<Derived, boost::mp11::mp_list<Ts...>, ThreadSafe>::own_parts() const
{ return OwnParts<false>(std::tuple_element_t<0, OwnParts<false>>(static_cast<const Derived&>(*this), m_impl->symbols)); }

template<typename Derived, bool ThreadSafe, typename... Ts>
typename SymbolContext<Derived, boost::mp11::mp_list<Ts...>, ThreadSafe>::template OwnParts<true>
SymbolContext<Derived, boost::mp11::mp_list<Ts...>, ThreadSafe>::own_parts()
{ return OwnParts<true>(std::tuple_element_t<0, OwnParts<true>>(static_cast<Derived&>(*this), m_impl->symbols)); }

}  // namespace ygg::formalism

#endif
