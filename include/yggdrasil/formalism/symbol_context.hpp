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

#ifndef YGG_FORMALISM_SYMBOL_CONTEXT_HPP_
#define YGG_FORMALISM_SYMBOL_CONTEXT_HPP_

#include "yggdrasil/formalism/builder.hpp"
#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/interning.hpp"
#include "yggdrasil/formalism/keyed.hpp"

#include <boost/mp11.hpp>
#include <cstddef>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>
#include <urd/elements/view.hpp>
#include <utility>

namespace ygg::formalism
{

/// The base of a repository that is the context of its records' views (urd::index_view<T, Derived>), with its symbols
/// behind a pointer: Derived (CRTP) adds what it holds besides, a view of each index resolves through it. This header
/// declares the members that touch the symbols; symbol_context_impl.hpp defines them and the stores (a plain symbol
/// repository), and a library instantiates them for each of its repositories in one source
/// (symbol_context_instantiations.hpp), so that its other sources complete no store.
/// Named by its record types and Derived, never by a store, so that no lookup completes one (rule 25).
///
/// Over a parent (its stores layered over the parent's), lookups and reads find the parent's records too.
template<typename Derived, typename Types, bool ThreadSafe = false>
class SymbolContext;

template<typename Derived, bool ThreadSafe, typename... Ts>
class SymbolContext<Derived, boost::mp11::mp_list<Ts...>, ThreadSafe> : public KeyedRepository<Derived>
{
public:
    static constexpr bool thread_safe = ThreadSafe;
    using SymbolTypes = boost::mp11::mp_list<Ts...>;
    /// The keys it resolves itself (operator[]): its symbols' indices.
    using KeyTypes = boost::mp11::mp_list<urd::index<Ts>...>;
    /// Its own part, as read (Writable false) or written into (key_parts): its symbols, behind its Impl.
    template<bool Writable>
    using OwnParts = std::tuple<
        HeldPart<detail::access_t<Writable, Derived>, detail::access_t<Writable, SymbolStores<ThreadSafe, Ts...>>, KeyTypes>>;

    /// The stores: defined in symbol_context_impl.hpp.
    struct Impl;

    // Its records are viewed (view(key), view(record)), stored and found (insert, find: of a record, a builder, a view)
    // by KeyedRepository's members, through its symbols: a view's context is the layer that owns its record.

    /// Forgets this layer's records; the parent's stay.
    void clear();

    /// The layer this one lies over, or nullptr.
    [[nodiscard]] const Derived* parent() const noexcept { return m_parent; }

    SymbolContext();
    /// Parent layers must remain frozen and outlive this one.
    explicit SymbolContext(const Derived* parent);
    ~SymbolContext();
    SymbolContext(const SymbolContext&) = delete;
    SymbolContext& operator=(const SymbolContext&) = delete;
    SymbolContext(SymbolContext&&) = delete;
    SymbolContext& operator=(SymbolContext&&) = delete;

private:
    friend class KeyedRepository<Derived>;

    /// Its own parts as read (OwnParts<false>: its symbols); KeyedRepository adds the repositories Derived nests.
    [[nodiscard]] OwnParts<false> own_parts() const;

    /// Its own parts as written into: its symbols.
    [[nodiscard]] OwnParts<true> own_parts();

    const Derived* m_parent = nullptr;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace ygg::formalism

#endif
