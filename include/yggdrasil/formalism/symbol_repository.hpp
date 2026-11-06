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
#include "yggdrasil/formalism/membership.hpp"

#include <cassert>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <urd/config.hpp>

namespace ygg::formalism
{
template<typename... Ts>
class SymbolRepository;

template<typename... Ts>
class ConcurrentSymbolRepository;

/// Owns symbol storage and publishes indexed views with the derived repository as context.
template<typename Repository, typename Types, bool ThreadSafe = false>
class SymbolRepositoryBase;

template<typename Repository, bool ThreadSafe, typename... Ts>
class SymbolRepositoryBase<Repository, boost::mp11::mp_list<Ts...>, ThreadSafe> : private BasicSymbolRepository<Ts, ThreadSafe>...
{
private:
    const SymbolRepositoryBase* m_parent;
    const SymbolRepositoryBase* m_root;

    [[nodiscard]] const Repository& repository() const noexcept { return static_cast<const Repository&>(*this); }

public:
    /// An index as its view here (urd's resolves contract): what compositions of indices are read through (urd::resolve).
    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    [[nodiscard]] urd::view<T, Repository> view(urd::index<T> index) const
    { return { index, get_canonical_context(index) }; }

    static constexpr bool thread_safe = ThreadSafe;

    using SymbolTypes = boost::mp11::mp_list<Ts...>;

private:
    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    [[nodiscard]] const SymbolRepositoryBase* find_canonical_context(urd::index<T> index) const noexcept
    {
        const auto* current = this;
        while (current != nullptr)
        {
            if (current->template get<T>().is_local(index))
                return current;
            current = current->m_parent;
        }
        return nullptr;
    }

public:
    /**
     * Global methods traverse the current repository layer and its parent
     * hierarchy. Handle-producing methods return views that retain the discovered
     * canonical context.
     */

    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    [[nodiscard]] std::optional<urd::view<T, Repository>> find_with_hash(const urd::data<T>& builder, size_t h) const noexcept
    {
        if (auto index_or_nullopt = this->template get<T>().find_local_with_hash(builder, h))
            return urd::view<T, Repository>(*index_or_nullopt, repository());

        const auto* current = m_parent;
        while (current != nullptr)
        {
            if (auto index_or_nullopt = current->template get<T>().find_local_with_hash(builder, h))
                return urd::view<T, Repository>(*index_or_nullopt, current->repository());

            current = current->m_parent;
        }

        return std::nullopt;
    }

    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    [[nodiscard]] std::optional<urd::view<T, Repository>> find(const urd::data<T>& builder) const noexcept
    { return find_with_hash(builder, SymbolRepositoryBase::hash(builder)); }

    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    std::pair<urd::view<T, Repository>, bool> insert(urd::data<T>& builder)
    {
        const auto h = SymbolRepositoryBase::hash(builder);
        if (auto view_or_nullopt = find_with_hash(builder, h))
        {
            builder.index = view_or_nullopt->index();
            return { *view_or_nullopt, false };
        }

        assert(!get<T>().exists_parent_mutation()
               && "Integrity error: Parent SymbolRepository modified after child "
                  "branching!");

        const auto [index, success] = create_local_with_hash(builder, h);
        return { urd::view<T, Repository>(index, repository()), success };
    }

    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    const urd::data<T>& operator[](urd::index<T> index) const
    {
        if (const auto* owner = find_canonical_context(index))
            return owner->template get<T>().at_local(index);
        throw std::out_of_range("Symbol index not found in any repository layer.");
    }

    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    [[nodiscard]] const urd::data<T>& front() const
    { return (*this)[urd::index<T>(0)]; }

    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    [[nodiscard]] size_t size() const noexcept
    { return get<T>().size(); }

    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    [[nodiscard]] const Repository& get_canonical_context(urd::index<T> index) const
    {
        if (const auto* owner = find_canonical_context(index))
            return owner->repository();
        throw std::out_of_range("Symbol index not found in any repository layer.");
    }

    template<typename T>
        requires SupportsSymbol<SymbolRepositoryBase, T>
    [[nodiscard]] bool contains(urd::index<T> index) const noexcept
    { return find_canonical_context(index) != nullptr; }

    template<typename V>
        requires requires(const Repository& repository, const V& view) { ::ygg::formalism::contains(repository, view); }
    [[nodiscard]] bool contains(const V& view) const
    { return ::ygg::formalism::contains(repository(), view); }

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
    explicit SymbolRepositoryBase(const Repository* parent) :
        BasicSymbolRepository<Ts, ThreadSafe>(parent ? &parent->template get<Ts>() : nullptr)...,
        m_parent(parent),
        m_root(m_parent ? m_parent->m_root : this)
    {
    }

    ~SymbolRepositoryBase() = default;
    SymbolRepositoryBase(const SymbolRepositoryBase&) = delete;
    SymbolRepositoryBase& operator=(const SymbolRepositoryBase&) = delete;
    SymbolRepositoryBase(SymbolRepositoryBase&&) = delete;
    SymbolRepositoryBase& operator=(SymbolRepositoryBase&&) = delete;

    [[nodiscard]] const Repository& get_root() const noexcept { return m_root->repository(); }

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
