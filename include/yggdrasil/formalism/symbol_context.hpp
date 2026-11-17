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

#include "yggdrasil/formalism/declarations.hpp"

#include <boost/mp11.hpp>
#include <cstddef>
#include <memory>
#include <optional>
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
class SymbolContext<Derived, boost::mp11::mp_list<Ts...>, ThreadSafe>
{
public:
    static constexpr bool thread_safe = ThreadSafe;
    using SymbolTypes = boost::mp11::mp_list<Ts...>;

    /// The stores: defined in symbol_context_impl.hpp.
    struct Impl;

    /// An index as its view here (urd's resolves contract): what compositions of indices are read through (urd::resolve).
    template<typename T>
        requires(boost::mp11::mp_contains<boost::mp11::mp_list<Ts...>, T>::value)
    [[nodiscard]] urd::index_view<T, Derived> view(urd::index<T> index) const noexcept
    { return { index, &static_cast<const Derived&>(*this) }; }

    template<typename T>
        requires(boost::mp11::mp_contains<boost::mp11::mp_list<Ts...>, T>::value)
    [[nodiscard]] std::optional<urd::index_view<T, Derived>> find(const urd::data<T>& builder) const
    { return do_find<T>(builder); }

    /// Interns the record; builder.index is set to its index.
    template<typename T>
        requires(boost::mp11::mp_contains<boost::mp11::mp_list<Ts...>, T>::value)
    std::pair<urd::index_view<T, Derived>, bool> insert(urd::data<T>& builder)
    { return do_insert<T>(builder); }

    template<typename T>
        requires(boost::mp11::mp_contains<boost::mp11::mp_list<Ts...>, T>::value)
    [[nodiscard]] const urd::data<T>& operator[](urd::index<T> index) const
    { return do_at<T>(index); }

    /// Whether this layer or a parent holds the index.
    template<typename T>
        requires(boost::mp11::mp_contains<boost::mp11::mp_list<Ts...>, T>::value)
    [[nodiscard]] bool contains(urd::index<T> index) const
    { return do_contains<T>(index); }

    /// The number of records of type T, along the layers.
    template<typename T>
        requires(boost::mp11::mp_contains<boost::mp11::mp_list<Ts...>, T>::value)
    [[nodiscard]] std::size_t size() const
    { return do_size<T>(); }

    /// The memory of this layer's records of type T, in bytes.
    template<typename T>
        requires(boost::mp11::mp_contains<boost::mp11::mp_list<Ts...>, T>::value)
    [[nodiscard]] std::size_t memory_usage() const
    { return do_memory_usage<T>(); }

    /// Forgets this layer's records; the parent's stay.
    void clear();

    SymbolContext();
    /// Parent layers must remain frozen and outlive this one.
    explicit SymbolContext(const Derived* parent);
    ~SymbolContext();
    SymbolContext(const SymbolContext&) = delete;
    SymbolContext& operator=(const SymbolContext&) = delete;
    SymbolContext(SymbolContext&&) = delete;
    SymbolContext& operator=(SymbolContext&&) = delete;

private:
    // What a library instantiates: the constrained members above, unconstrained, which they forward to. Clang names an
    // instantiation's requires-clause in its symbol and g++ does not, so only unconstrained symbols link between code
    // built by either.
    template<typename T>
    [[nodiscard]] std::optional<urd::index_view<T, Derived>> do_find(const urd::data<T>& builder) const;
    template<typename T>
    std::pair<urd::index_view<T, Derived>, bool> do_insert(urd::data<T>& builder);
    template<typename T>
    [[nodiscard]] const urd::data<T>& do_at(urd::index<T> index) const;
    template<typename T>
    [[nodiscard]] bool do_contains(urd::index<T> index) const;
    template<typename T>
    [[nodiscard]] std::size_t do_size() const;
    template<typename T>
    [[nodiscard]] std::size_t do_memory_usage() const;

    std::unique_ptr<Impl> m_impl;
};

}  // namespace ygg::formalism

#endif
