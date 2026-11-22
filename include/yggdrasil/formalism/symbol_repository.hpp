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

#ifndef YGG_FORMALISM_SYMBOL_REPOSITORY_HPP_
#define YGG_FORMALISM_SYMBOL_REPOSITORY_HPP_

#include "yggdrasil/core/family.hpp"
#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/keyed_impl.hpp"

#include <boost/mp11.hpp>
#include <optional>
#include <tuple>
#include <type_traits>
#include <urd/config.hpp>
#include <urd/elements/flat.hpp>
#include <urd/memory.hpp>
#include <urd/repr/records/arena.hpp>
#include <urd/repr/records/records.hpp>
#include <urd/repr/wrappers/layered_pool.hpp>
#include <urd/store.hpp>
#include <utility>

namespace ygg::formalism
{

/// The records of type T in one layer: a urd pool of urd::data<T>, each once, handing out index<T>, stored flat with
/// references that hold while others are inserted (views borrow them); layered over a parent layer's pool, its indices
/// continuing after the parent's, its lookups finding the parent's records too.
template<typename T, bool ThreadSafe = false>
using SymbolStore = urd::Store<urd::pool<urd::data<T>, urd::index<T>>,
                               urd::flat,
                               urd::stable_refs,
                               urd::layered,
                               std::conditional_t<ThreadSafe, urd::concurrent, urd::single_threaded>>;

/// Interns `record` in `store` (a pool of urd::data<T> handing out index<T>): its index anywhere along the store's
/// layers, and whether it was stored now. record.index is set to that index; a new record is stored with it.
template<typename Store, typename T>
std::pair<urd::index<T>, bool> intern(Store& store, urd::data<T>& record)
{
    const auto [at, created] = store.insert(record,
                                            [&record](urd::index<T> given) -> const urd::data<T>&
                                            {
                                                record.index = given;
                                                return record;
                                            });
    record.index = at;
    return { at, created };
}

template<typename... Ts>
class SymbolRepository;

template<typename... Ts>
class ConcurrentSymbolRepository;

/// The symbols of a repository, one SymbolStore per type in a urd family, read through the derived Repository: views
/// of its indices have it as context, which reads their records through its one key lookup (KeyedRepository, over its
/// family as its one part: urd's fold selects the type's store first). Over a parent (its stores layered over the parent's), lookups and reads find the
/// parent's records too.
template<typename Repository, typename Types, bool ThreadSafe = false>
class SymbolRepositoryBase;

template<typename Repository, bool ThreadSafe, typename... Ts>
class SymbolRepositoryBase<Repository, boost::mp11::mp_list<Ts...>, ThreadSafe> : public KeyedRepository<Repository>
{
    template<typename T>
    using Store = SymbolStore<T, ThreadSafe>;

    Family<Store, Ts...> m_symbols;

    [[nodiscard]] const Repository& repository() const noexcept { return static_cast<const Repository&>(*this); }

public:
    static constexpr bool thread_safe = ThreadSafe;

    using SymbolTypes = boost::mp11::mp_list<Ts...>;
    /// The keys it resolves (operator[], at()): its symbols' indices.
    using KeyTypes = boost::mp11::mp_list<urd::index<Ts>...>;
    /// Its one part that resolves keys: its family of stores, held in place; and the same, written into (its records
    /// stored and found by KeyedRepository's one insert and find).
    using OwnParts = std::tuple<HeldPart<const Repository, const Family<Store, Ts...>, KeyTypes>>;
    using OwnWritableParts = std::tuple<HeldPart<Repository, Family<Store, Ts...>, KeyTypes>>;

    /// An index as its view here (urd's resolves contract): what compositions of indices are read through (urd::resolve).
    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    [[nodiscard]] urd::index_view<T, Repository> view(urd::index<T> index) const noexcept
    { return { index, &repository() }; }


    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    [[nodiscard]] size_t size() const
    { return get<T>().size(); }

    /// The number of records of type T this layer holds itself (its parents' not counted).
    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    [[nodiscard]] size_t own_size() const
    { return get<T>().size() - get<T>().base(); }

    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    [[nodiscard]] size_t memory_usage() const
    { return urd::memory_usage(get<T>()); }

    /// The store of T's records.
    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    [[nodiscard]] Store<T>& get()
    { return m_symbols.template get<T>(); }

    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    [[nodiscard]] const Store<T>& get() const
    { return m_symbols.template get<T>(); }

private:
    friend class KeyedRepository<Repository>;

    [[nodiscard]] OwnParts own_parts() const { return OwnParts(std::tuple_element_t<0, OwnParts>(repository(), m_symbols)); }

    [[nodiscard]] OwnWritableParts own_writable_parts()
    { return OwnWritableParts(std::tuple_element_t<0, OwnWritableParts>(static_cast<Repository&>(*this), m_symbols)); }

public:
    SymbolRepositoryBase() : SymbolRepositoryBase(nullptr) {}

    /// Parent layers must remain frozen and outlive this repository.
    explicit SymbolRepositoryBase(const Repository* parent) :
        m_symbols(parent == nullptr ? nullptr : &static_cast<const SymbolRepositoryBase*>(parent)->m_symbols)
    {
    }

    ~SymbolRepositoryBase() = default;
    SymbolRepositoryBase(const SymbolRepositoryBase&) = delete;
    SymbolRepositoryBase& operator=(const SymbolRepositoryBase&) = delete;
    SymbolRepositoryBase(SymbolRepositoryBase&&) = delete;
    SymbolRepositoryBase& operator=(SymbolRepositoryBase&&) = delete;

    void clear() { m_symbols.clear(); }
};

template<typename... Ts>
class SymbolRepository : public SymbolRepositoryBase<SymbolRepository<Ts...>, boost::mp11::mp_list<Ts...>>
{
    using Base = SymbolRepositoryBase<SymbolRepository<Ts...>, boost::mp11::mp_list<Ts...>>;

public:
    SymbolRepository() : Base(nullptr) {}

    explicit SymbolRepository(const SymbolRepository* parent) : Base(parent) {}

    friend const SymbolRepository& get_repository(const SymbolRepository& repository URD_LIFETIMEBOUND) noexcept { return repository; }
};

template<typename... Ts>
class ConcurrentSymbolRepository : public SymbolRepositoryBase<ConcurrentSymbolRepository<Ts...>, boost::mp11::mp_list<Ts...>, true>
{
    using Base = SymbolRepositoryBase<ConcurrentSymbolRepository<Ts...>, boost::mp11::mp_list<Ts...>, true>;

public:
    ConcurrentSymbolRepository() : Base(nullptr) {}

    explicit ConcurrentSymbolRepository(const ConcurrentSymbolRepository* parent) : Base(parent) {}

    friend const ConcurrentSymbolRepository& get_repository(const ConcurrentSymbolRepository& repository URD_LIFETIMEBOUND) noexcept { return repository; }
};

}  // namespace ygg::formalism

#endif
