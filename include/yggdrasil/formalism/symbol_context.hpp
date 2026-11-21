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

    /// A record of its kinds held in place (a child held by value), as its view here; never a temporary.
    template<typename T>
        requires(boost::mp11::mp_contains<boost::mp11::mp_list<Ts...>, T>::value)
    [[nodiscard]] urd::borrowed_view<T, Derived> view(const urd::data<T>& record URD_LIFETIMEBOUND) const noexcept
    { return { &record, &static_cast<const Derived&>(*this) }; }

    template<typename T>
        requires(boost::mp11::mp_contains<boost::mp11::mp_list<Ts...>, T>::value)
    void view(const urd::data<T>&& record) const = delete;

    template<typename T>
        requires(boost::mp11::mp_contains<boost::mp11::mp_list<Ts...>, T>::value)
    [[nodiscard]] std::optional<urd::index_view<T, Derived>> find(const urd::data<T>& record) const
    { return do_find<T>(record); }

    /// Interns the record; record.index is set to its index.
    template<typename T>
        requires(boost::mp11::mp_contains<boost::mp11::mp_list<Ts...>, T>::value)
    std::pair<urd::index_view<T, Derived>, bool> insert(urd::data<T>& record)
    { return do_insert<T>(record); }

    /// Interns a builder through the scratch records of a builder storage that holds them (its scratch<T>(), each worker's own; no
    /// allocation once they have grown; the records of the repositories nested in this one from the same storage): its built children and records first (each
    /// prepared, as the repository's domain makes it canonical, and inserted), interned children kept as they are, then its record, written into a scratch
    /// record, prepared and inserted. The view of its record, and whether it was new. A child interned in another repository of this kind, or a child of
    /// another repository not interned there, throws std::invalid_argument.
    template<typename T, IsBuilderStorage Storage>
        requires requires(Storage& storage) { storage.template scratch<T>(); }
    decltype(auto) insert(const urd::builder<T, Derived>& built, Storage& storage)
    { return detail::insert_child(static_cast<Derived&>(*this), storage, built); }

    /// Finds the record a builder writes, inserting nothing: its children looked up the same way (a child not found
    /// names no record here, so then nothing is found; a record of its own is prepared in place), then its record.
    template<typename T, IsBuilderStorage Storage>
        requires requires(Storage& storage) { storage.template scratch<T>(); }
    found_t<Derived, T> find(const urd::builder<T, Derived>& built, Storage& storage)
    { return detail::find_child(static_cast<Derived&>(*this), storage, built); }

    /// Interns a view of a record of its kinds as it reads it (what a child resolves to): an interned one is checked
    /// to be its own, a builder's is interned as that builder.
    template<ViewThrough<Derived> V, IsBuilderStorage Storage>
    decltype(auto) insert(const V& view, Storage& storage)
    { return detail::insert_view(static_cast<Derived&>(*this), storage, view); }

    /// Finds the record a view reads, inserting nothing, likewise.
    template<ViewThrough<Derived> V, IsBuilderStorage Storage>
    auto find(const V& view, Storage& storage)
    { return detail::find_view(static_cast<Derived&>(*this), storage, view); }

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

    /// The layer this one lies over, or nullptr.
    [[nodiscard]] const Derived* parent() const noexcept { return m_parent; }

    /// Whether a key of `source` names the same record here (urd::Sees): `source` is this layer or one it lies over.
    [[nodiscard]] bool sees(const Derived& source) const noexcept
    {
        for (const Derived* layer = &static_cast<const Derived&>(*this); layer != nullptr; layer = layer->parent())
        {
            if (layer == &source)
                return true;
        }
        return false;
    }

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
    [[nodiscard]] std::optional<urd::index_view<T, Derived>> do_find(const urd::data<T>& record) const;
    template<typename T>
    std::pair<urd::index_view<T, Derived>, bool> do_insert(urd::data<T>& record);
    template<typename T>
    [[nodiscard]] const urd::data<T>& do_at(urd::index<T> index) const;
    template<typename T>
    [[nodiscard]] bool do_contains(urd::index<T> index) const;
    template<typename T>
    [[nodiscard]] std::size_t do_size() const;
    template<typename T>
    [[nodiscard]] std::size_t do_memory_usage() const;

    const Derived* m_parent = nullptr;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace ygg::formalism

#endif
