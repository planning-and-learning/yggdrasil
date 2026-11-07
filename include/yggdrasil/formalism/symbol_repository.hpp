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

#include <boost/mp11.hpp>
#include <urd/elements/flat.hpp>
#include "yggdrasil/formalism/basic_symbol_repository.hpp"
#include "yggdrasil/formalism/declarations.hpp"

#include <optional>
#include <type_traits>
#include <utility>
#include <urd/config.hpp>

namespace ygg::formalism
{
template<typename... Ts>
class SymbolRepository;

template<typename... Ts>
class ConcurrentSymbolRepository;

/// One layer of symbol storage, read through its local API; views of its indices have the derived repository as
/// context. The parent pointer only numbers the layer's indices after its parent's (parent_size); walking the parent
/// chain is Repository's job (repository.hpp).
template<typename Repository, typename Types, bool ThreadSafe = false>
class SymbolRepositoryBase;

template<typename Repository, bool ThreadSafe, typename... Ts>
class SymbolRepositoryBase<Repository, boost::mp11::mp_list<Ts...>, ThreadSafe> : private BasicSymbolRepository<Ts, ThreadSafe>...
{
private:
    [[nodiscard]] const Repository& repository() const noexcept { return static_cast<const Repository&>(*this); }

public:
    /// An index as its view here (urd's resolves contract): what compositions of indices are read through (urd::resolve).
    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    [[nodiscard]] urd::index_view<T, Repository> view(urd::index<T> index) const noexcept
    { return { index, &repository() }; }

    static constexpr bool thread_safe = ThreadSafe;

    using SymbolTypes = boost::mp11::mp_list<Ts...>;

    /// The record's view in this layer, if this layer holds it.
    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    [[nodiscard]] std::optional<urd::index_view<T, Repository>> find(const urd::data<T>& builder) const noexcept
    {
        if (const auto index = get<T>().find_local(builder))
            return repository().view(*index);
        return std::nullopt;
    }

    /// Interns the record in this layer; builder.index is set to its index.
    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    std::pair<urd::index_view<T, Repository>, bool> insert(urd::data<T>& builder)
    {
        const auto [index, created] = get<T>().insert_local(builder);
        return { repository().view(index), created };
    }

    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    [[nodiscard]] size_t size() const noexcept
    { return get<T>().size(); }

    /**
     * Local methods access only the current repository layer.
     * Handle-producing methods return raw handles because the caller already
     * knows the context.
     */

    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    BasicSymbolRepository<T, ThreadSafe>& get() noexcept
    { return static_cast<BasicSymbolRepository<T, ThreadSafe>&>(*this); }

    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    [[nodiscard]] const BasicSymbolRepository<T, ThreadSafe>& get() const noexcept
    { return static_cast<const BasicSymbolRepository<T, ThreadSafe>&>(*this); }

    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    [[nodiscard]] auto find_local_with_hash(const urd::data<T>& builder, size_t h) const noexcept
    { return get<T>().find_local_with_hash(builder, h); }

    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    [[nodiscard]] auto find_local(const urd::data<T>& builder) const noexcept
    { return get<T>().find_local(builder); }

    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    auto insert_local_with_hash(urd::data<T>& builder, size_t h)
    { return get<T>().insert_local_with_hash(builder, h); }

    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    std::pair<urd::index<T>, bool> create_local_with_hash(urd::data<T>& builder, size_t h)
    { return get<T>().create_local_with_hash(builder, h); }

    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    auto insert_local(urd::data<T>& builder)
    { return get<T>().insert_local(builder); }

    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    [[nodiscard]] const urd::data<T>& local(urd::index<T> index) const noexcept
    { return get<T>().local(index); }

    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    [[nodiscard]] const urd::data<T>& at_local(urd::index<T> index) const
    { return get<T>().at_local(index); }

    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    const urd::data<T>& front_local() const
    { return get<T>().front_local(); }

    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    [[nodiscard]] size_t local_size() const noexcept
    { return get<T>().local_size(); }

    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    [[nodiscard]] size_t parent_size() const noexcept
    { return get<T>().parent_size(); }

    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    [[nodiscard]] bool is_local(urd::index<T> index) const noexcept
    { return get<T>().is_local(index); }

    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    [[nodiscard]] bool exists_parent_mutation() const noexcept
    { return get<T>().exists_parent_mutation(); }

    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    [[nodiscard]] size_t memory_usage() const noexcept
    { return get<T>().memory_usage(); }

    /**
     * Common methods do not depend on lookup scope.
     */

    SymbolRepositoryBase() : SymbolRepositoryBase(nullptr) {}

    /// Parent layers must remain frozen and outlive this repository.
    explicit SymbolRepositoryBase(const Repository* parent) : BasicSymbolRepository<Ts, ThreadSafe>(parent ? &parent->template get<Ts>() : nullptr)... {}

    ~SymbolRepositoryBase() = default;
    SymbolRepositoryBase(const SymbolRepositoryBase&) = delete;
    SymbolRepositoryBase& operator=(const SymbolRepositoryBase&) = delete;
    SymbolRepositoryBase(SymbolRepositoryBase&&) = delete;
    SymbolRepositoryBase& operator=(SymbolRepositoryBase&&) = delete;

    void clear() noexcept { (this->template get<Ts>().clear(), ...); }

    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    static size_t hash(const urd::data<T>& builder) noexcept
    { return BasicSymbolRepository<T, ThreadSafe>::hash(builder); }
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
