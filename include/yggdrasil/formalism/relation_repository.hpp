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

#ifndef YGG_FORMALISM_RELATION_REPOSITORY_HPP_
#define YGG_FORMALISM_RELATION_REPOSITORY_HPP_

#include <cassert>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <yggdrasil/core/types.hpp>
#include <yggdrasil/formalism/basic_relation_repository.hpp>
#include <yggdrasil/formalism/declarations.hpp>
#include <yggdrasil/formalism/object_index.hpp>

namespace ygg::formalism
{
template<typename ObjectTag, typename... Ts>
class RelationRepository;

template<typename ObjectTag, typename... Ts>
class ConcurrentRelationRepository;

namespace detail
{
template<typename Repository, typename ObjectTag, bool ThreadSafe, typename... Ts>
class RelationRepositoryBase : private BasicRelationRepository<ObjectTag, Ts, ThreadSafe>...
{
private:
    template<typename T, typename...>
    struct FirstType
    {
        using type = T;
    };

    const RelationRepositoryBase* m_parent;
    const RelationRepositoryBase* m_root;
    size_t m_index;

    const Repository& repository() const noexcept { return static_cast<const Repository&>(*this); }

public:
    using object_tag = ObjectTag;
    using container_type = typename BasicRelationRepository<ObjectTag, typename FirstType<Ts...>::type, ThreadSafe>::container_type;
    using ConstViewType = typename container_type::ConstArrayView;
    static constexpr bool thread_safe = ThreadSafe;

    /**
     * Global methods traverse the current repository layer and its parent hierarchy.
     * Handle-producing methods return views that retain the discovered canonical context.
     */

    template<typename T>
    std::optional<View<Index<RelationBinding<T, ObjectTag>>, Repository>> find_with_hash(const Data<RelationBinding<T, ObjectTag>>& builder, size_t h) const
    {
        const auto relation = builder.relation;

        if (auto row_or_nullopt = this->template get<T>().find_local_with_hash(builder, h))
            return View<Index<RelationBinding<T, ObjectTag>>, Repository>(Index<RelationBinding<T, ObjectTag>> { relation, *row_or_nullopt }, repository());

        const auto* current = m_parent;
        while (current != nullptr)
        {
            if (auto row_or_nullopt = current->template get<T>().find_local_unsafe_with_hash(builder, h))
                return View<Index<RelationBinding<T, ObjectTag>>, Repository>(Index<RelationBinding<T, ObjectTag>> { relation, *row_or_nullopt },
                                                                              current->repository());

            current = current->m_parent;
        }

        return std::nullopt;
    }

    template<typename T>
    std::optional<View<Index<RelationBinding<T, ObjectTag>>, Repository>> find(const Data<RelationBinding<T, ObjectTag>>& builder) const
    {
        return find_with_hash(builder, RelationRepositoryBase::hash(builder));
    }

    template<typename T>
    std::pair<View<Index<RelationBinding<T, ObjectTag>>, Repository>, bool> get_or_create(const Data<RelationBinding<T, ObjectTag>>& builder)
    {
        const auto relation = builder.relation;
        const auto h = RelationRepositoryBase::hash(builder);

        if (auto view_or_nullopt = find_with_hash(builder, h))
            return { *view_or_nullopt, false };

        assert(!get<T>().exists_parent_mutation(relation)
               && "Integrity error: Parent RelationRepository modified after child "
                  "branching!");

        const auto [row, success] = create_local_with_hash(builder, h);
        return { View<Index<RelationBinding<T, ObjectTag>>, Repository>(Index<RelationBinding<T, ObjectTag>> { relation, row }, repository()), success };
    }

    template<typename T>
    auto operator[](Index<RelationBinding<T, ObjectTag>> index) const
    {
        const auto* current = this;
        while (current != nullptr)
        {
            if (current->template get<T>().is_local(index))
                return current->template get<T>().at_local(index);

            current = current->m_parent;
        }

        throw std::out_of_range("Relation binding index not found in any repository layer.");
    }

    template<typename T>
    auto front(Index<T> g) const
    {
        return (*this)[Index<RelationBinding<T, ObjectTag>> { g, Index<Row>(0) }];
    }

    template<typename T>
    size_t size(Index<T> g) const noexcept
    {
        return get<T>().size(g);
    }

    template<typename T>
    const Repository& get_canonical_context(Index<RelationBinding<T, ObjectTag>> index) const
    {
        const auto* current = this;
        while (current != nullptr)
        {
            if (current->template get<T>().is_local(index))
                return current->repository();

            current = current->m_parent;
        }

        throw std::out_of_range("Relation binding index not found in any repository layer.");
    }

    /**
     * Local methods access only the current repository layer.
     * Handle-producing methods return raw handles because the caller already knows the context.
     */

    template<typename T>
    BasicRelationRepository<ObjectTag, T, ThreadSafe>& get() noexcept
    {
        return static_cast<BasicRelationRepository<ObjectTag, T, ThreadSafe>&>(*this);
    }

    template<typename T>
    const BasicRelationRepository<ObjectTag, T, ThreadSafe>& get() const noexcept
    {
        return static_cast<const BasicRelationRepository<ObjectTag, T, ThreadSafe>&>(*this);
    }

    template<typename T>
    auto find_local_with_hash(const Data<RelationBinding<T, ObjectTag>>& builder, size_t h) const
    {
        return get<T>().find_local_with_hash(builder, h);
    }

    /// Forwards the unsafe local-only lookup without traversing ancestors.
    template<typename T>
    auto find_local_unsafe_with_hash(const Data<RelationBinding<T, ObjectTag>>& builder, size_t h) const
    {
        return get<T>().find_local_unsafe_with_hash(builder, h);
    }

    template<typename T>
    auto find_local(const Data<RelationBinding<T, ObjectTag>>& builder) const
    {
        return get<T>().find_local(builder);
    }

    template<typename T>
    auto get_or_create_local_with_hash(const Data<RelationBinding<T, ObjectTag>>& builder, size_t h)
    {
        return get<T>().get_or_create_local_with_hash(builder, h);
    }

    template<typename T>
    std::pair<Index<Row>, bool> create_local_with_hash(const Data<RelationBinding<T, ObjectTag>>& builder, size_t h)
    {
        return get<T>().create_local_with_hash(builder, h);
    }

    template<typename T>
    auto get_or_create_local(const Data<RelationBinding<T, ObjectTag>>& builder)
    {
        return get<T>().get_or_create_local(builder);
    }

    template<typename T>
    auto at_local(Index<RelationBinding<T, ObjectTag>> index) const
    {
        return get<T>().at_local(index);
    }

    template<typename T>
    auto front_local(Index<T> g) const
    {
        return get<T>().front_local(g);
    }

    template<typename T>
    size_t local_size(Index<T> g) const noexcept
    {
        return get<T>().local_size(g);
    }

    template<typename T>
    size_t parent_size(Index<T> g) const noexcept
    {
        return get<T>().parent_size(g);
    }

    template<typename T>
    bool is_local(Index<RelationBinding<T, ObjectTag>> index) const noexcept
    {
        return get<T>().is_local(index);
    }

    template<typename T>
    bool exists_parent_mutation(Index<T> g) const noexcept
    {
        return get<T>().exists_parent_mutation(g);
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
    RelationRepositoryBase(size_t index, const Repository* parent = nullptr) : RelationRepositoryBase(index, parent, RelationRepositoryConfig()) {}

    RelationRepositoryBase(size_t index, const Repository* parent, RelationRepositoryConfig config) :
        BasicRelationRepository<ObjectTag, Ts, ThreadSafe>(parent ? &parent->template get<Ts>() : nullptr, config)...,
        m_parent(parent),
        m_root(m_parent ? m_parent->m_root : this),
        m_index(index)
    {
    }

    RelationRepositoryBase(const RelationRepositoryBase&) = delete;
    RelationRepositoryBase& operator=(const RelationRepositoryBase&) = delete;
    RelationRepositoryBase(RelationRepositoryBase&&) = delete;
    RelationRepositoryBase& operator=(RelationRepositoryBase&&) = delete;

    const auto& get_index() const noexcept { return m_index; }
    const Repository& get_root() const noexcept { return m_root->repository(); }
    std::uint8_t get_object_index_width() const noexcept { return this->template get<typename FirstType<Ts...>::type>().get_object_index_width(); }

    void clear() noexcept { (this->template get<Ts>().clear(), ...); }

    template<typename T>
    static size_t hash(const Data<RelationBinding<T, ObjectTag>>& builder) noexcept
    {
        return BasicRelationRepository<ObjectTag, T, ThreadSafe>::hash(builder);
    }
};
}  // namespace detail

template<typename ObjectTag, typename... Ts>
class RelationRepository : public detail::RelationRepositoryBase<RelationRepository<ObjectTag, Ts...>, ObjectTag, false, Ts...>
{
    using Base = detail::RelationRepositoryBase<RelationRepository<ObjectTag, Ts...>, ObjectTag, false, Ts...>;

public:
    RelationRepository(size_t index, const RelationRepository* parent = nullptr) : Base(index, parent) {}
    RelationRepository(size_t index, const RelationRepository* parent, RelationRepositoryConfig config) : Base(index, parent, config) {}
};

template<typename ObjectTag, typename... Ts>
class ConcurrentRelationRepository : public detail::RelationRepositoryBase<ConcurrentRelationRepository<ObjectTag, Ts...>, ObjectTag, true, Ts...>
{
    using Base = detail::RelationRepositoryBase<ConcurrentRelationRepository<ObjectTag, Ts...>, ObjectTag, true, Ts...>;

public:
    ConcurrentRelationRepository(size_t index, const ConcurrentRelationRepository* parent = nullptr) : Base(index, parent) {}
    ConcurrentRelationRepository(size_t index, const ConcurrentRelationRepository* parent, RelationRepositoryConfig config) : Base(index, parent, config) {}
};
}  // namespace ygg::formalism

#endif
