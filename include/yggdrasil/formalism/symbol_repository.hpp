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

#include <cassert>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <yggdrasil/buffer/declarations.hpp>
#include <yggdrasil/core/types.hpp>
#include <yggdrasil/formalism/basic_symbol_repository.hpp>
#include <yggdrasil/formalism/declarations.hpp>

namespace ygg::formalism
{
template<typename... Ts>
class SymbolRepository;

template<typename... Ts>
class ConcurrentSymbolRepository;

namespace detail
{
template<typename Repository, bool ThreadSafe, typename... Ts>
class SymbolRepositoryBase : private BasicSymbolRepository<Ts, ThreadSafe>...
{
private:
    const SymbolRepositoryBase* m_parent;
    const SymbolRepositoryBase* m_root;

    const Repository& repository() const noexcept { return static_cast<const Repository&>(*this); }

public:
    static constexpr bool thread_safe = ThreadSafe;

    /**
     * Global methods traverse the current repository layer and its parent hierarchy.
     * Handle-producing methods return views that retain the discovered canonical context.
     */

    template<typename T>
    std::optional<View<Index<T>, Repository>> find_with_hash(const Data<T>& builder, size_t h) const noexcept
    {
        if (auto index_or_nullopt = this->template get<T>().find_local_with_hash(builder, h))
            return View<Index<T>, Repository>(*index_or_nullopt, repository());

        const auto* current = m_parent;
        while (current != nullptr)
        {
            if (auto index_or_nullopt = current->template get<T>().find_local_unsafe_with_hash(builder, h))
                return View<Index<T>, Repository>(*index_or_nullopt, current->repository());

            current = current->m_parent;
        }

        return std::nullopt;
    }

    template<typename T>
    std::optional<View<Index<T>, Repository>> find(const Data<T>& builder) const noexcept
    {
        return find_with_hash(builder, SymbolRepositoryBase::hash(builder));
    }

    template<typename T>
    std::pair<View<Index<T>, Repository>, bool> get_or_create(Data<T>& builder)
    {
        const auto h = SymbolRepositoryBase::hash(builder);
        if (auto view_or_nullopt = find_with_hash(builder, h))
            return { *view_or_nullopt, false };

        assert(!get<T>().exists_parent_mutation()
               && "Integrity error: Parent SymbolRepository modified after child "
                  "branching!");

        const auto [index, success] = create_local_with_hash(builder, h);
        return { View<Index<T>, Repository>(index, repository()), success };
    }

    template<typename T>
    const Data<T>& operator[](Index<T> index) const
    {
        const auto* current = this;
        while (current != nullptr)
        {
            if (current->template get<T>().is_local(index))
                return current->template get<T>().at_local(index);

            current = current->m_parent;
        }

        throw std::out_of_range("Symbol index not found in any repository layer.");
    }

    template<typename T>
    const Data<T>& front() const
    {
        return (*this)[Index<T>(0)];
    }

    template<typename T>
    size_t size() const noexcept
    {
        return get<T>().size();
    }

    template<typename T>
    const Repository& get_canonical_context(Index<T> index) const
    {
        const auto* current = this;
        while (current != nullptr)
        {
            if (current->template get<T>().is_local(index))
                return current->repository();

            current = current->m_parent;
        }

        throw std::out_of_range("Symbol index not found in any repository layer.");
    }

    /**
     * Local methods access only the current repository layer.
     * Handle-producing methods return raw handles because the caller already knows the context.
     */

    template<typename T>
    BasicSymbolRepository<T, ThreadSafe>& get() noexcept
    {
        return static_cast<BasicSymbolRepository<T, ThreadSafe>&>(*this);
    }

    template<typename T>
    const BasicSymbolRepository<T, ThreadSafe>& get() const noexcept
    {
        return static_cast<const BasicSymbolRepository<T, ThreadSafe>&>(*this);
    }

    template<typename T>
    auto find_local_with_hash(const Data<T>& builder, size_t h) const noexcept
    {
        return get<T>().find_local_with_hash(builder, h);
    }

    /// Forwards the unsafe local-only lookup without traversing ancestors.
    template<typename T>
    auto find_local_unsafe_with_hash(const Data<T>& builder, size_t h) const noexcept
    {
        return get<T>().find_local_unsafe_with_hash(builder, h);
    }

    template<typename T>
    auto find_local(const Data<T>& builder) const noexcept
    {
        return get<T>().find_local(builder);
    }

    template<typename T>
    auto get_or_create_local_with_hash(Data<T>& builder, size_t h)
    {
        return get<T>().get_or_create_local_with_hash(builder, h);
    }

    template<typename T>
    std::pair<Index<T>, bool> create_local_with_hash(Data<T>& builder, size_t h)
    {
        return get<T>().create_local_with_hash(builder, h);
    }

    template<typename T>
    auto get_or_create_local(Data<T>& builder)
    {
        return get<T>().get_or_create_local(builder);
    }

    template<typename T>
    const Data<T>& at_local(Index<T> index) const
    {
        return get<T>().at_local(index);
    }

    template<typename T>
    const Data<T>& front_local() const
    {
        return get<T>().front_local();
    }

    template<typename T>
    size_t local_size() const noexcept
    {
        return get<T>().local_size();
    }

    template<typename T>
    size_t parent_size() const noexcept
    {
        return get<T>().parent_size();
    }

    template<typename T>
    bool is_local(Index<T> index) const noexcept
    {
        return get<T>().is_local(index);
    }

    template<typename T>
    bool exists_parent_mutation() const noexcept
    {
        return get<T>().exists_parent_mutation();
    }

    template<typename T>
    size_t memory_usage() const noexcept
    {
        return get<T>().memory_usage();
    }

    /**
     * Common methods do not depend on lookup scope.
     */

    /// Parent layers must remain frozen and outlive this repository.
    SymbolRepositoryBase(const Repository* parent = nullptr) :
        BasicSymbolRepository<Ts, ThreadSafe>(parent ? &parent->template get<Ts>() : nullptr)...,
        m_parent(parent),
        m_root(m_parent ? m_parent->m_root : this)
    {
    }

    SymbolRepositoryBase(const SymbolRepositoryBase&) = delete;
    SymbolRepositoryBase& operator=(const SymbolRepositoryBase&) = delete;
    SymbolRepositoryBase(SymbolRepositoryBase&&) = delete;
    SymbolRepositoryBase& operator=(SymbolRepositoryBase&&) = delete;

    const Repository& get_root() const noexcept { return m_root->repository(); }

    void clear() noexcept { (this->template get<Ts>().clear(), ...); }

    template<typename T>
    static size_t hash(const Data<T>& builder) noexcept
    {
        return BasicSymbolRepository<T, ThreadSafe>::hash(builder);
    }
};
}  // namespace detail

template<typename... Ts>
class SymbolRepository : public detail::SymbolRepositoryBase<SymbolRepository<Ts...>, false, Ts...>
{
    using Base = detail::SymbolRepositoryBase<SymbolRepository<Ts...>, false, Ts...>;

public:
    SymbolRepository(const SymbolRepository* parent = nullptr) : Base(parent) {}
};

template<typename... Ts>
class ConcurrentSymbolRepository : public detail::SymbolRepositoryBase<ConcurrentSymbolRepository<Ts...>, true, Ts...>
{
    using Base = detail::SymbolRepositoryBase<ConcurrentSymbolRepository<Ts...>, true, Ts...>;

public:
    ConcurrentSymbolRepository(const ConcurrentSymbolRepository* parent = nullptr) : Base(parent) {}
};

}  // namespace ygg::formalism

#endif
