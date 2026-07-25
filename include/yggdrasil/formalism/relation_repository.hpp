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
#include <tuple>
#include <type_traits>
#include <utility>
#include <yggdrasil/containers/tuple.hpp>
#include <yggdrasil/core/types.hpp>
#include <yggdrasil/formalism/basic_relation_repository.hpp>
#include <yggdrasil/formalism/declarations.hpp>
#include <yggdrasil/formalism/object_index.hpp>

namespace ygg::formalism
{
template<typename ObjectTag, typename... Ts>
class RelationRepository
{
private:
    const RelationRepository* m_parent;
    const RelationRepository* m_root;
    std::tuple<BasicRelationRepository<ObjectTag, Ts>...> m_repositories;
    size_t m_index;

public:
    using object_tag = ObjectTag;
    using container_type = typename BasicRelationRepository<ObjectTag, std::tuple_element_t<0, std::tuple<Ts...>>>::container_type;
    using ConstViewType = typename container_type::ConstArrayView;

    /**
     * Global methods traverse the current repository layer and its parent hierarchy.
     * Handle-producing methods return views that retain the discovered canonical context.
     */

    template<typename T>
    std::optional<View<Index<RelationBinding<T, ObjectTag>>, RelationRepository>> find_with_hash(const Data<RelationBinding<T, ObjectTag>>& builder,
                                                                                                 size_t h) const noexcept
    {
        const auto relation = builder.relation;

        const auto* current = this;
        while (current != nullptr)
        {
            if (auto row_or_nullopt = current->template get<T>().find_local_with_hash(builder, h))
                return View<Index<RelationBinding<T, ObjectTag>>, RelationRepository>(Index<RelationBinding<T, ObjectTag>> { relation, *row_or_nullopt },
                                                                                      *current);

            current = current->m_parent;
        }

        return std::nullopt;
    }

    template<typename T>
    std::optional<View<Index<RelationBinding<T, ObjectTag>>, RelationRepository>> find(const Data<RelationBinding<T, ObjectTag>>& builder) const noexcept
    {
        return find_with_hash(builder, RelationRepository::hash(builder));
    }

    template<typename T>
    std::pair<View<Index<RelationBinding<T, ObjectTag>>, RelationRepository>, bool> get_or_create(const Data<RelationBinding<T, ObjectTag>>& builder)
    {
        const auto relation = builder.relation;
        const auto h = RelationRepository::hash(builder);

        if (auto view_or_nullopt = find_with_hash(builder, h))
            return { *view_or_nullopt, false };

        assert(!get<T>().exists_parent_mutation(relation)
               && "Integrity error: Parent RelationRepository modified after child "
                  "branching!");

        const auto [row, success] = get<T>().get_or_create_local_with_hash(builder, h);
        return { View<Index<RelationBinding<T, ObjectTag>>, RelationRepository>(Index<RelationBinding<T, ObjectTag>> { relation, row }, *this), success };
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
    const RelationRepository& get_canonical_context(Index<RelationBinding<T, ObjectTag>> index) const
    {
        const auto* current = this;
        while (current != nullptr)
        {
            if (current->template get<T>().is_local(index))
                return *current;

            current = current->m_parent;
        }

        throw std::out_of_range("Relation binding index not found in any repository layer.");
    }

    /**
     * Local methods access only the current repository layer.
     * Handle-producing methods return raw handles because the caller already knows the context.
     */

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
    auto insert_new_local_with_hash(const Data<RelationBinding<T, ObjectTag>>& builder, size_t h)
    {
        return get<T>().insert_new_local_with_hash(builder, h);
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

    /**
     * Common methods do not depend on lookup scope.
     */

    RelationRepository(size_t index, const RelationRepository* parent = nullptr) :
        m_parent(parent),
        m_root(m_parent ? m_parent->m_root : this),
        m_repositories(BasicRelationRepository<ObjectTag, Ts>(parent ? &std::get<BasicRelationRepository<ObjectTag, Ts>>(parent->m_repositories) : nullptr)...),
        m_index(index)
    {
    }

    RelationRepository(const RelationRepository&) = delete;
    RelationRepository& operator=(const RelationRepository&) = delete;
    RelationRepository(RelationRepository&&) = delete;
    RelationRepository& operator=(RelationRepository&&) = delete;

    const auto& get_index() const noexcept { return m_index; }
    const auto& get_root() const noexcept { return *m_root; }

    void clear() noexcept
    {
        std::apply([](auto&... repos) { (repos.clear(), ...); }, m_repositories);
    }

    template<typename T>
    static size_t hash(const Data<RelationBinding<T, ObjectTag>>& builder) noexcept
    {
        return BasicRelationRepository<ObjectTag, T>::hash(builder);
    }
};
}  // namespace ygg::formalism

#endif
