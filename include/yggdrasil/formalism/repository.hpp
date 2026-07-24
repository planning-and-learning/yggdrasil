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

#ifndef YGG_FORMALISM_REPOSITORY_HPP_
#define YGG_FORMALISM_REPOSITORY_HPP_

#include <cassert>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <yggdrasil/buffer/declarations.hpp>
#include <yggdrasil/buffer/segmented_buffer.hpp>
#include <yggdrasil/containers/tuple.hpp>
#include <yggdrasil/formalism/declarations.hpp>
#include <yggdrasil/formalism/relation_repository.hpp>
#include <yggdrasil/formalism/symbol_repository.hpp>
#include <yggdrasil/semantics/equal_to.hpp>
#include <yggdrasil/semantics/hash.hpp>

namespace ygg::formalism
{

template<typename SymbolRepo, typename RelationRepo>
class Repository
{
private:
    const Repository* m_parent;
    const Repository* m_root;
    SymbolRepo m_symbol_repository;
    RelationRepo m_relation_repository;
    size_t m_index;

    /**
     * Global methods traverse the current repository layer and its parent hierarchy.
     * Handle-producing methods return views that retain the discovered canonical context.
     *
     * Symbol operations.
     */

    template<typename T>
        requires NonRelationBindingConcept<T>
    std::optional<View<Index<T>, Repository>> find_with_hash(const Data<T>& builder, size_t h) const noexcept
    {
        const Repository* current = this;
        while (current != nullptr)
        {
            if (auto index_or_nullopt = current->m_symbol_repository.find_local_with_hash(builder, h))
                return View<Index<T>, Repository>(*index_or_nullopt, *current);

            current = current->m_parent;
        }

        return std::nullopt;
    }

    template<typename T>
        requires NonRelationBindingConcept<T>
    std::pair<View<Index<T>, Repository>, bool> get_or_create_with_hash(Data<T>& builder, size_t h)
    {
        const Repository* current = this;
        while (current != nullptr)
        {
            if (auto index_or_nullopt = current->m_symbol_repository.find_local_with_hash(builder, h))
                return { View<Index<T>, Repository>(*index_or_nullopt, *current), false };
            current = current->m_parent;
        }

        assert(!m_symbol_repository.template exists_parent_mutation<T>()
               && "Integrity error: Parent SymbolRepository modified after child "
                  "branching!");

        const auto [index, success] = m_symbol_repository.get_or_create_local_with_hash(builder, h);
        return { View<Index<T>, Repository>(index, *this), success };
    }

    /**
     * Global methods traverse the current repository layer and its parent hierarchy.
     * Handle-producing methods return views that retain the discovered canonical context.
     *
     * Relation operations.
     */

    template<typename T>
    std::optional<View<Index<RelationBinding<T, typename RelationRepo::object_tag>>, Repository>>
    find_with_hash(const Data<RelationBinding<T, typename RelationRepo::object_tag>>& builder, size_t h) const noexcept
    {
        const auto g = builder.relation;

        const Repository* current = this;
        while (current != nullptr)
        {
            if (auto row_or_nullopt = current->m_relation_repository.find_local_with_hash(builder, h))
                return View<Index<RelationBinding<T, typename RelationRepo::object_tag>>, Repository>(
                    Index<RelationBinding<T, typename RelationRepo::object_tag>> { g, *row_or_nullopt },
                    *current);

            current = current->m_parent;
        }

        return std::nullopt;
    }

    template<typename T>
    std::pair<View<Index<RelationBinding<T, typename RelationRepo::object_tag>>, Repository>, bool>
    get_or_create_with_hash(const Data<RelationBinding<T, typename RelationRepo::object_tag>>& builder, size_t h)
    {
        const auto g = builder.relation;

        const Repository* current = this;
        while (current != nullptr)
        {
            if (auto row_or_nullopt = current->m_relation_repository.find_local_with_hash(builder, h))
                return { View<Index<RelationBinding<T, typename RelationRepo::object_tag>>, Repository>(
                             Index<RelationBinding<T, typename RelationRepo::object_tag>> { g, *row_or_nullopt },
                             *current),
                         false };
            current = current->m_parent;
        }

        assert(!m_relation_repository.exists_parent_mutation(g)
               && "Integrity error: Parent RelationRepository modified after child "
                  "branching!");

        const auto [row, success] = m_relation_repository.get_or_create_local_with_hash(builder, h);
        return { View<Index<RelationBinding<T, typename RelationRepo::object_tag>>, Repository>(
                     Index<RelationBinding<T, typename RelationRepo::object_tag>> { g, row },
                     *this),
                 success };
    }

public:
    /**
     * Global methods traverse the current repository layer and its parent hierarchy.
     * Handle-producing methods return views that retain the discovered canonical context.
     *
     * Symbol operations.
     */

    template<typename T>
        requires NonRelationBindingConcept<T>
    std::optional<View<Index<T>, Repository>> find(const Data<T>& builder) const noexcept
    {
        return find_with_hash(builder, SymbolRepo::hash(builder));
    }

    template<typename T>
        requires NonRelationBindingConcept<T>
    std::pair<View<Index<T>, Repository>, bool> get_or_create(Data<T>& builder)
    {
        return get_or_create_with_hash(builder, SymbolRepo::hash(builder));
    }

    template<typename T>
        requires NonRelationBindingConcept<T>
    const Data<T>& operator[](Index<T> index) const
    {
        const Repository* current = this;
        while (current != nullptr)
        {
            if (current->m_symbol_repository.is_local(index))
                return current->m_symbol_repository.at_local(index);

            current = current->m_parent;
        }
        throw std::out_of_range("Symbol index not found in any repository layer.");
    }

    template<typename T>
        requires NonRelationBindingConcept<T>
    const Data<T>& front() const
    {
        const Repository* current = this;
        while (current->m_parent != nullptr)
        {
            if (current->m_symbol_repository.template parent_size<T>() == 0)
                break;
            current = current->m_parent;
        }

        return current->m_symbol_repository.template front_local<T>();
    }

    template<typename T>
        requires NonRelationBindingConcept<T>
    size_t size() const noexcept
    {
        return m_symbol_repository.template size<T>();
    }

    template<typename T>
        requires NonRelationBindingConcept<T>
    const Repository& get_canonical_context(Index<T> index) const
    {
        const Repository* current = this;
        while (current != nullptr)
        {
            if (current->m_symbol_repository.is_local(index))
                return *current;

            current = current->m_parent;
        }

        throw std::out_of_range("Symbol index not found in any repository layer.");
    }

    /**
     * Global methods traverse the current repository layer and its parent hierarchy.
     * Handle-producing methods return views that retain the discovered canonical context.
     *
     * Relation operations.
     */

    template<typename T>
    std::optional<View<Index<RelationBinding<T, typename RelationRepo::object_tag>>, Repository>>
    find(const Data<RelationBinding<T, typename RelationRepo::object_tag>>& builder) const noexcept
    {
        return find_with_hash(builder, RelationRepo::hash(builder));
    }

    template<typename T>
    std::pair<View<Index<RelationBinding<T, typename RelationRepo::object_tag>>, Repository>, bool>
    get_or_create(const Data<RelationBinding<T, typename RelationRepo::object_tag>>& builder)
    {
        return get_or_create_with_hash(builder, RelationRepo::hash(builder));
    }

    template<typename T>
    auto operator[](Index<RelationBinding<T, typename RelationRepo::object_tag>> index) const
    {
        const Repository* current = this;
        while (current != nullptr)
        {
            if (current->m_relation_repository.is_local(index))
                return current->m_relation_repository.at_local(index);

            current = current->m_parent;
        }
        throw std::out_of_range("Relation binding index not found in any repository layer.");
    }

    template<typename T>
    auto front(Index<T> g) const
    {
        const Repository* current = this;
        while (current->m_parent != nullptr)
        {
            if (current->m_relation_repository.parent_size(g) == 0)
                break;
            current = current->m_parent;
        }

        return current->m_relation_repository.front_local(g);
    }

    template<typename T>
    size_t size(Index<T> g) const noexcept
    {
        return m_relation_repository.parent_size(g) + m_relation_repository.local_size(g);
    }

    template<typename T>
    const Repository& get_canonical_context(Index<RelationBinding<T, typename RelationRepo::object_tag>> index) const
    {
        const Repository* current = this;
        while (current != nullptr)
        {
            if (current->m_relation_repository.is_local(index))
                return *current;

            current = current->m_parent;
        }

        throw std::out_of_range("Relation binding index not found in any repository layer.");
    }

    /**
     * Common methods do not depend on lookup scope.
     */

    Repository(size_t index, const Repository* parent = nullptr) :
        m_parent(parent),
        m_root(m_parent ? m_parent->m_root : this),
        m_symbol_repository(m_parent ? &m_parent->m_symbol_repository : nullptr),
        m_relation_repository(index, m_parent ? &m_parent->m_relation_repository : nullptr),
        m_index(index)
    {
        clear();
    }
    Repository(const Repository& other) = delete;
    Repository& operator=(const Repository& other) = delete;
    Repository(Repository&& other) = delete;
    Repository& operator=(Repository&& other) = delete;

    const auto& get_index() const noexcept { return m_index; }
    const auto& get_root() const noexcept { return *m_root; }

    void clear() noexcept
    {
        m_symbol_repository.clear();
        m_relation_repository.clear();
    }
};

}  // namespace ygg::formalism

#endif
