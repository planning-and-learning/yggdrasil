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

#include "yggdrasil/formalism/binding_view.hpp"
#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/membership.hpp"
#include "yggdrasil/formalism/relation_repository.hpp"
#include "yggdrasil/formalism/symbol_repository.hpp"

#include <cassert>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <urd/elements/equal_to.hpp>
#include <urd/elements/hash.hpp>
#include <utility>
#include <urd/config.hpp>

namespace ygg::formalism
{

/// Concurrent repository aliases support overlapping find, insert,
/// published-index lookup, and size calls. Builders remain thread-confined;
/// clear, memory accounting, and destruction require quiescence. Parent layers
/// must remain frozen and outlive their descendants.
template<typename SymbolRepo, typename RelationRepo>
class Repository
{
public:
    using SymbolTypes = typename SymbolRepo::SymbolTypes;
    using RelationTypes = typename RelationRepo::RelationTypes;
    using object_tag = typename RelationRepo::object_tag;

private:
    const Repository* m_parent;
    const Repository* m_root;
    SymbolRepo m_symbol_repository;
    RelationRepo m_relation_repository;
    size_t m_index;

    template<typename T>
        requires NonRelationBindingConcept<T> && SupportsSymbol<Repository, T>
    [[nodiscard]] const Repository* find_canonical_context(urd::index<T> index) const noexcept
    {
        const auto* current = this;
        while (current != nullptr)
        {
            if (current->m_symbol_repository.is_local(index))
                return current;
            current = current->m_parent;
        }
        return nullptr;
    }

    template<typename T>
        requires SupportsRelation<Repository, T>
    [[nodiscard]] const Repository* find_canonical_context(ygg::formalism::RelationBindingIndex<T, object_tag> index) const noexcept
    {
        const auto* current = this;
        while (current != nullptr)
        {
            if (current->m_relation_repository.is_local(index))
                return current;
            current = current->m_parent;
        }
        return nullptr;
    }

    /**
     * Global methods traverse the current repository layer and its parent
     * hierarchy. Handle-producing methods return views that retain the discovered
     * canonical context.
     *
     * Symbol operations.
     */

    template<typename T>
        requires NonRelationBindingConcept<T> && SupportsSymbol<Repository, T>
    [[nodiscard]] std::optional<urd::index_view<T, Repository>> find_with_hash(const urd::data<T>& builder, size_t h) const noexcept
    {
        if (auto index_or_nullopt = m_symbol_repository.find_local_with_hash(builder, h))
            return urd::index_view<T, Repository>(*index_or_nullopt, this);

        const Repository* current = m_parent;
        while (current != nullptr)
        {
            if (auto index_or_nullopt = current->m_symbol_repository.find_local_with_hash(builder, h))
                return urd::index_view<T, Repository>(*index_or_nullopt, current);

            current = current->m_parent;
        }

        return std::nullopt;
    }

    /// Cold half of insert_with_hash, see URD_NOINLINE. Rechecks the local
    /// layer before publishing.
    template<typename T>
        requires NonRelationBindingConcept<T> && SupportsSymbol<Repository, T>
    URD_NOINLINE std::pair<urd::index_view<T, Repository>, bool> create_local_with_hash(urd::data<T>& builder, size_t h)
    {
        assert(!m_symbol_repository.template exists_parent_mutation<T>()
               && "Integrity error: Parent SymbolRepository modified after child "
                  "branching!");

        const auto [index, created] = m_symbol_repository.create_local_with_hash(builder, h);
        return { urd::index_view<T, Repository>(index, this), created };
    }

    template<typename T>
        requires NonRelationBindingConcept<T> && SupportsSymbol<Repository, T>
    std::pair<urd::index_view<T, Repository>, bool> insert_with_hash(urd::data<T>& builder, size_t h)
    {
        if (auto found = find_with_hash(builder, h))
        {
            builder.index = found->index();
            return { *found, false };
        }

        return create_local_with_hash(builder, h);
    }

    /**
     * Global methods traverse the current repository layer and its parent
     * hierarchy. Handle-producing methods return views that retain the discovered
     * canonical context.
     *
     * Relation operations.
     */

    template<typename T>
        requires SupportsRelation<Repository, T>
    [[nodiscard]] std::optional<::ygg::formalism::RelationBindingView<ygg::formalism::RelationBindingIndex<T, object_tag>, Repository>> find_with_hash(const urd::data<RelationBinding<T, object_tag>>& builder,
                                                                                                        size_t h) const
    {
        const auto g = builder.relation;

        if (auto row_or_nullopt = m_relation_repository.find_local_with_hash(builder, h))
            return ::ygg::formalism::RelationBindingView<ygg::formalism::RelationBindingIndex<T, object_tag>, Repository>(ygg::formalism::RelationBindingIndex<T, object_tag> { g, *row_or_nullopt }, this);

        const Repository* current = m_parent;
        while (current != nullptr)
        {
            if (auto row_or_nullopt = current->m_relation_repository.find_local_with_hash(builder, h))
                return ::ygg::formalism::RelationBindingView<ygg::formalism::RelationBindingIndex<T, object_tag>, Repository>(ygg::formalism::RelationBindingIndex<T, object_tag> { g, *row_or_nullopt }, current);

            current = current->m_parent;
        }

        return std::nullopt;
    }

    template<typename T>
        requires SupportsRelation<Repository, T>
    std::pair<::ygg::formalism::RelationBindingView<ygg::formalism::RelationBindingIndex<T, object_tag>, Repository>, bool> insert_with_hash(const urd::data<RelationBinding<T, object_tag>>& builder, size_t h)
    {
        if (auto found = find_with_hash(builder, h))
            return { *found, false };

        return create_local_with_hash(builder, h);
    }

    /// Cold half of insert_with_hash, see URD_NOINLINE. Rechecks the local
    /// lane before publishing.
    template<typename T>
        requires SupportsRelation<Repository, T>
    URD_NOINLINE std::pair<::ygg::formalism::RelationBindingView<ygg::formalism::RelationBindingIndex<T, object_tag>, Repository>, bool>
    create_local_with_hash(const urd::data<RelationBinding<T, object_tag>>& builder, size_t h)
    {
        const auto g = builder.relation;

        assert(!m_relation_repository.exists_parent_mutation(g)
               && "Integrity error: Parent RelationRepository modified after child "
                  "branching!");

        const auto [row, created] = m_relation_repository.create_local_with_hash(builder, h);
        return { ::ygg::formalism::RelationBindingView<ygg::formalism::RelationBindingIndex<T, object_tag>, Repository>(ygg::formalism::RelationBindingIndex<T, object_tag> { g, row }, this), created };
    }

public:
    static constexpr bool thread_safe = SymbolRepo::thread_safe && RelationRepo::thread_safe;

    friend const Repository& get_repository(const Repository& repository URD_LIFETIMEBOUND) noexcept { return repository; }

    /**
     * Global methods traverse the current repository layer and its parent
     * hierarchy. Handle-producing methods return views that retain the discovered
     * canonical context.
     *
     * Symbol operations.
     */

    template<typename T>
        requires NonRelationBindingConcept<T> && SupportsSymbol<Repository, T>
    [[nodiscard]] std::optional<urd::index_view<T, Repository>> find(const urd::data<T>& builder) const noexcept
    { return find_with_hash(builder, SymbolRepo::hash(builder)); }

    template<typename T>
        requires NonRelationBindingConcept<T> && SupportsSymbol<Repository, T>
    std::pair<urd::index_view<T, Repository>, bool> insert(urd::data<T>& builder)
    { return insert_with_hash(builder, SymbolRepo::hash(builder)); }

    template<typename T>
        requires NonRelationBindingConcept<T> && SupportsSymbol<Repository, T>
    const urd::data<T>& operator[](urd::index<T> index) const
    {
        if (const auto* owner = find_canonical_context(index))
            return owner->m_symbol_repository.at_local(index);
        throw std::out_of_range("Symbol index not found in any repository layer.");
    }

    template<typename T>
        requires NonRelationBindingConcept<T> && SupportsSymbol<Repository, T>
    [[nodiscard]] const urd::data<T>& front() const
    { return (*this)[urd::index<T>(0)]; }

    template<typename T>
        requires NonRelationBindingConcept<T> && SupportsSymbol<Repository, T>
    [[nodiscard]] size_t size() const noexcept
    { return m_symbol_repository.template size<T>(); }

    template<typename T>
        requires NonRelationBindingConcept<T> && SupportsSymbol<Repository, T>
    [[nodiscard]] size_t memory_usage() const noexcept
    { return m_symbol_repository.template memory_usage<T>(); }

    template<typename T>
        requires NonRelationBindingConcept<T> && SupportsSymbol<Repository, T>
    [[nodiscard]] bool contains(urd::index<T> index) const noexcept
    { return find_canonical_context(index) != nullptr; }

    /// An index as its view here (urd's resolves contract): what compositions of indices are read through (urd::resolve).
    template<typename T>
        requires NonRelationBindingConcept<T> && SupportsSymbol<Repository, T>
    [[nodiscard]] urd::index_view<T, Repository> view(urd::index<T> index) const
    { return { index, &get_canonical_context(index) }; }
    /// A record held outside the repository as its view here, which points at it (the record must outlive the view;
    /// never a temporary).
    template<typename T>
    [[nodiscard]] urd::borrowed_view<T, Repository> view(const urd::data<T>& record URD_LIFETIMEBOUND) const
    { return { &record, this }; }
    template<typename T>
    void view(const urd::data<T>&& record) const = delete;
    /// A binding as its view, through the layer that owns it.
    template<typename T>
        requires SupportsRelation<Repository, T>
    [[nodiscard]] RelationBindingView<RelationBindingIndex<T, object_tag>, Repository> view(RelationBindingIndex<T, object_tag> index) const
    { return { index, &get_canonical_context(index) }; }

    template<typename T>
        requires NonRelationBindingConcept<T> && SupportsSymbol<Repository, T>
    [[nodiscard]] const Repository& get_canonical_context(urd::index<T> index) const
    {
        if (const auto* owner = find_canonical_context(index))
            return *owner;
        throw std::out_of_range("Symbol index not found in any repository layer.");
    }

    /**
     * Global methods traverse the current repository layer and its parent
     * hierarchy. Handle-producing methods return views that retain the discovered
     * canonical context.
     *
     * Relation operations.
     */

    template<typename T>
        requires SupportsRelation<Repository, T>
    [[nodiscard]] std::optional<::ygg::formalism::RelationBindingView<ygg::formalism::RelationBindingIndex<T, object_tag>, Repository>> find(const urd::data<RelationBinding<T, object_tag>>& builder) const
    { return find_with_hash(builder, RelationRepo::hash(builder)); }

    template<typename T>
        requires SupportsRelation<Repository, T>
    std::pair<::ygg::formalism::RelationBindingView<ygg::formalism::RelationBindingIndex<T, object_tag>, Repository>, bool> insert(const urd::data<RelationBinding<T, object_tag>>& builder)
    { return insert_with_hash(builder, RelationRepo::hash(builder)); }

    template<typename T>
        requires SupportsRelation<Repository, T>
    auto operator[](ygg::formalism::RelationBindingIndex<T, object_tag> index) const
    {
        if (const auto* owner = find_canonical_context(index))
        {
            auto objects = owner->m_relation_repository.at_local(index);
            return RelationBindingRow<T, decltype(objects)> { index.relation, std::move(objects) };
        }
        throw std::out_of_range("Relation binding index not found in any repository layer.");
    }

    template<typename T>
        requires SupportsRelation<Repository, T>
    [[nodiscard]] auto front(urd::index<T> g) const
    { return (*this)[ygg::formalism::RelationBindingIndex<T, object_tag> { g, urd::index<Row>(0) }]; }

    template<typename T>
        requires SupportsRelation<Repository, T>
    [[nodiscard]] size_t size(urd::index<T> g) const noexcept
    { return m_relation_repository.size(g); }

    template<RelationBindingConcept T>
        requires SupportsRelation<Repository, typename T::relation_tag> && std::same_as<typename T::object_tag, object_tag>
    [[nodiscard]] size_t memory_usage() const noexcept
    {
        using Binding = std::remove_cvref_t<T>;
        return m_relation_repository.template memory_usage<typename Binding::relation_tag>();
    }

    template<typename T>
        requires SupportsRelation<Repository, T>
    [[nodiscard]] bool contains(ygg::formalism::RelationBindingIndex<T, object_tag> index) const noexcept
    { return find_canonical_context(index) != nullptr; }

    template<typename T>
        requires SupportsRelation<Repository, T>
    [[nodiscard]] const Repository& get_canonical_context(ygg::formalism::RelationBindingIndex<T, object_tag> index) const
    {
        if (const auto* owner = find_canonical_context(index))
            return *owner;
        throw std::out_of_range("Relation binding index not found in any repository layer.");
    }

    template<typename V>
        requires requires(const Repository& repository, const V& view) { ::ygg::formalism::contains(repository, view); }
    [[nodiscard]] bool contains(const V& view) const
    { return ::ygg::formalism::contains(*this, view); }

    /**
     * Common methods do not depend on lookup scope.
     */

    /// Parent layers must remain frozen and outlive this repository.
    explicit Repository(size_t index, const Repository* parent = nullptr) :
        m_parent(parent),
        m_root(m_parent ? m_parent->m_root : this),
        m_symbol_repository(m_parent ? &m_parent->m_symbol_repository : nullptr),
        m_relation_repository(index, m_parent ? &m_parent->m_relation_repository : nullptr),
        m_index(index)
    { clear(); }

    Repository(size_t index, const Repository* parent, RelationRepositoryConfig config) :
        m_parent(parent),
        m_root(m_parent ? m_parent->m_root : this),
        m_symbol_repository(m_parent ? &m_parent->m_symbol_repository : nullptr),
        m_relation_repository(index, m_parent ? &m_parent->m_relation_repository : nullptr, config),
        m_index(index)
    { clear(); }
    ~Repository() = default;
    Repository(const Repository& other) = delete;
    Repository& operator=(const Repository& other) = delete;
    Repository(Repository&& other) = delete;
    Repository& operator=(Repository&& other) = delete;

    [[nodiscard]] const auto& get_index() const noexcept { return m_index; }
    /// The repository's number, as urd's views ask of a context.
    [[nodiscard]] std::size_t layer() const noexcept { return m_index; }
    [[nodiscard]] const auto& get_root() const noexcept { return *m_root; }
    [[nodiscard]] std::uint8_t get_object_index_width() const noexcept { return m_relation_repository.get_object_index_width(); }

    void clear() noexcept
    {
        m_symbol_repository.clear();
        m_relation_repository.clear();
    }
};

}  // namespace ygg::formalism

#endif
