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

#ifndef YGGDRASIL_FORMALISM_RELATION_REPOSITORY_HPP_
#define YGGDRASIL_FORMALISM_RELATION_REPOSITORY_HPP_

#include <yggdrasil/containers/tuple.hpp>
#include <yggdrasil/core/types.hpp>
#include <yggdrasil/formalism/basic_relation_repository.hpp>
#include <yggdrasil/formalism/declarations.hpp>
#include <yggdrasil/formalism/object_index.hpp>

#include <cassert>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace ygg::formalism
{
template<typename ObjectTag, typename... Ts>
class RelationRepository
{
private:
    const RelationRepository* m_parent;
    const RelationRepository* m_root;
    std::tuple<BasicRelationRepository<ObjectTag, Ts>...> m_repositories;

public:
    using object_tag = ObjectTag;
    using container_type = typename BasicRelationRepository<ObjectTag, std::tuple_element_t<0, std::tuple<Ts...>>>::container_type;
    using ConstViewType = typename container_type::ConstArrayView;

    RelationRepository(const RelationRepository* parent = nullptr) :
        m_parent(parent),
        m_root(m_parent ? m_parent->m_root : this),
        m_repositories(BasicRelationRepository<ObjectTag, Ts>(parent ? &std::get<BasicRelationRepository<ObjectTag, Ts>>(parent->m_repositories) : nullptr)...)
    {
    }

    RelationRepository(const RelationRepository&) = delete;
    RelationRepository& operator=(const RelationRepository&) = delete;
    RelationRepository(RelationRepository&&) = delete;
    RelationRepository& operator=(RelationRepository&&) = delete;

    const auto& get_root() const noexcept { return *m_root; }

    template<typename T>
    BasicRelationRepository<ObjectTag, T>& get() noexcept
    {
        return std::get<BasicRelationRepository<ObjectTag, T>>(m_repositories);
    }

    template<typename T>
    const BasicRelationRepository<ObjectTag, T>& get() const noexcept
    {
        return std::get<BasicRelationRepository<ObjectTag, T>>(m_repositories);
    }

    void clear() noexcept
    {
        std::apply([](auto&... repos) { (repos.clear(), ...); }, m_repositories);
    }

    template<typename T>
    static size_t hash(const Data<RelationBinding<T, ObjectTag>>& builder) noexcept
    {
        return BasicRelationRepository<ObjectTag, T>::hash(builder);
    }

    template<typename T>
    auto find_with_hash(const Data<RelationBinding<T, ObjectTag>>& builder, size_t h) const noexcept
    {
        return get<T>().find_with_hash(builder, h);
    }

    template<typename T>
    auto find(const Data<RelationBinding<T, ObjectTag>>& builder) const noexcept
    {
        return get<T>().find(builder);
    }

    template<typename T>
    auto get_or_create(const Data<RelationBinding<T, ObjectTag>>& builder)
    {
        return get<T>().get_or_create(builder);
    }

    template<typename T>
    auto operator[](Index<RelationBinding<T, ObjectTag>> index) const noexcept
    {
        return get<T>()[index];
    }

    template<typename T>
    size_t size(Index<T> g) const noexcept
    {
        return get<T>().size(g);
    }

    template<typename T>
    const BasicRelationRepository<ObjectTag, T>& get_canonical_context(Index<RelationBinding<T, ObjectTag>> index) const noexcept
    {
        return get<T>().get_canonical_context(index);
    }

    template<typename T>
    auto find_local_with_hash(const Data<RelationBinding<T, ObjectTag>>& builder, size_t h) const noexcept
    {
        return get<T>().find_local_with_hash(builder, h);
    }

    template<typename T>
    auto find_local(const Data<RelationBinding<T, ObjectTag>>& builder) const noexcept
    {
        return get<T>().find_local(builder);
    }

    template<typename T>
    auto get_or_create_local_with_hash(const Data<RelationBinding<T, ObjectTag>>& builder, size_t h)
    {
        return get<T>().get_or_create_local_with_hash(builder, h);
    }

    template<typename T>
    auto get_or_create_local(const Data<RelationBinding<T, ObjectTag>>& builder)
    {
        return get<T>().get_or_create_local(builder);
    }

    template<typename T>
    auto at_local(Index<RelationBinding<T, ObjectTag>> index) const noexcept
    {
        return get<T>().at_local(index);
    }

    template<typename T>
    auto front_local(Index<T> g) const noexcept
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
};
}

#endif
