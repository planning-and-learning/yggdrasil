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

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <urd/config.hpp>
#include <utility>

namespace ygg::formalism
{

/// A repository layer: symbols and relation bindings, over the layers of a parent, whose records it finds too. Views
/// of its handles have as context the layer that holds the record (its canonical context).
///
/// Concurrent repository aliases support overlapping find, insert, published-index lookup, and size calls. Builders
/// remain thread-confined; clear, memory accounting, and destruction require quiescence. Parent layers must remain
/// frozen and outlive their descendants.
template<typename SymbolRepo, typename RelationRepo>
class Repository
{
public:
    using SymbolTypes = typename SymbolRepo::SymbolTypes;
    using RelationTypes = typename RelationRepo::RelationTypes;
    using object_tag = typename RelationRepo::object_tag;

private:
    template<typename T>
    using BindingIndex = RelationBindingIndex<T, object_tag>;
    template<typename T>
    using BindingView = RelationBindingView<BindingIndex<T>, Repository>;

    const Repository* m_parent;
    const Repository* m_root;
    SymbolRepo m_symbol_repository;
    RelationRepo m_relation_repository;
    size_t m_index;

    /// The layer whose own records hold `index`: where the layer's own indices start (its base) is past it above.
    template<typename T>
        requires SupportsSymbol<Repository, T>
    [[nodiscard]] const Repository* find_canonical_context(urd::index<T> index) const
    {
        if (!contains(index))
            return nullptr;
        const auto* layer = this;
        while ((index.value) < layer->m_symbol_repository.template get<T>().base())
            layer = layer->m_parent;
        return layer;
    }

    /// The layer whose own pool of the relation holds the binding's row: in one walk up the layers, the nearest whose
    /// own pool starts at or before the row; it holds the row where the row is below its size.
    template<typename T>
        requires SupportsRelation<Repository, T>
    [[nodiscard]] const Repository* find_canonical_context(BindingIndex<T> index) const
    {
        for (const auto* layer = this; layer != nullptr; layer = layer->m_parent)
        {
            // Whether the row is in this layer's own pool, where it starts at or before the row; else none.
            const auto held = layer->m_relation_repository.template get<T>().read_own(
                index.relation,
                [&index](const auto& rows) -> std::optional<bool>
                { return (index.row.value) >= rows.base() ? std::optional((index.row.value) < rows.size()) : std::nullopt; },
                std::optional<bool>());
            if (held)
                return *held ? layer : nullptr;
        }
        return nullptr;
    }

public:
    static constexpr bool thread_safe = SymbolRepo::thread_safe && RelationRepo::thread_safe;

    friend const Repository& get_repository(const Repository& repository URD_LIFETIMEBOUND) noexcept { return repository; }

    /**
     * Symbols: found in this layer and its parents; views have the canonical context.
     */

    template<typename T>
        requires SupportsSymbol<Repository, T>
    [[nodiscard]] std::optional<urd::index_view<T, Repository>> find(const urd::data<T>& builder) const
    {
        if (const auto index = m_symbol_repository.template get<T>().find(builder); !index.is_none())
            return view(index);
        return std::nullopt;
    }

    /// Interns the record where no layer holds it; builder.index is set to its index.
    template<typename T>
        requires SupportsSymbol<Repository, T>
    std::pair<urd::index_view<T, Repository>, bool> insert(urd::data<T>& builder)
    {
        const auto [index, created] = intern(m_symbol_repository.template get<T>(), builder);
        return { view(index), created };
    }

    template<typename T>
        requires SupportsSymbol<Repository, T>
    const urd::data<T>& operator[](urd::index<T> index) const
    {
        if (!contains(index))
            throw std::out_of_range("Symbol index not found in any repository layer.");
        return m_symbol_repository[index];
    }

    template<typename T>
        requires SupportsSymbol<Repository, T>
    [[nodiscard]] size_t size() const
    { return m_symbol_repository.template size<T>(); }

    template<typename T>
        requires SupportsSymbol<Repository, T>
    [[nodiscard]] size_t memory_usage() const
    { return m_symbol_repository.template memory_usage<T>(); }

    template<typename T>
        requires SupportsSymbol<Repository, T>
    [[nodiscard]] bool contains(urd::index<T> index) const
    { return m_symbol_repository.contains(index); }

    /// An index as its view here (urd's resolves contract): what compositions of indices are read through (urd::resolve).
    template<typename T>
        requires SupportsSymbol<Repository, T>
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
    [[nodiscard]] BindingView<T> view(BindingIndex<T> index) const
    { return { index, &get_canonical_context(index) }; }

    template<typename T>
        requires SupportsSymbol<Repository, T>
    [[nodiscard]] const Repository& get_canonical_context(urd::index<T> index) const
    {
        if (const auto* owner = find_canonical_context(index))
            return *owner;
        throw std::out_of_range("Symbol index not found in any repository layer.");
    }

    /**
     * Relation bindings: found in this layer and its parents; views have the canonical context.
     */

    template<typename T>
        requires SupportsRelation<Repository, T>
    [[nodiscard]] std::optional<BindingView<T>> find(const urd::data<RelationBinding<T, object_tag>>& builder) const
    {
        if (const auto row = m_relation_repository.find(builder); !row.is_none())
            return view(BindingIndex<T> { builder.relation, row });
        return std::nullopt;
    }

    /// Stores the binding in this layer where no layer holds it.
    template<typename T>
        requires SupportsRelation<Repository, T>
    std::pair<BindingView<T>, bool> insert(const urd::data<RelationBinding<T, object_tag>>& builder)
    {
        if (auto found = find(builder))
            return { *found, false };
        const auto [row, created] = m_relation_repository.insert(builder);
        return { BindingView<T>(BindingIndex<T> { builder.relation, row }, this), created };
    }

    template<typename T>
        requires SupportsRelation<Repository, T>
    auto operator[](BindingIndex<T> index) const
    {
        const auto& rows = m_relation_repository.template get<T>().at(index.relation);
        if ((index.row.value) >= rows.size())
            throw std::out_of_range("Relation binding index not found in any repository layer.");
        auto objects = rows.view(index.row);
        return RelationBindingRow<T, decltype(objects)> { index.relation, std::move(objects) };
    }

    template<typename T>
        requires SupportsRelation<Repository, T>
    [[nodiscard]] size_t size(urd::index<T> g) const
    { return m_relation_repository.template get<T>().read(g, [](const auto& rows) { return size_t(rows.size()); }, size_t { 0 }); }

    template<RelationBindingConcept T>
        requires SupportsRelation<Repository, typename T::relation_tag> && std::same_as<typename T::object_tag, object_tag>
    [[nodiscard]] size_t memory_usage() const
    { return m_relation_repository.template memory_usage<typename T::relation_tag>(); }

    template<typename T>
        requires SupportsRelation<Repository, T>
    [[nodiscard]] bool contains(BindingIndex<T> index) const
    { return !index.relation.is_none() && (index.row.value) < size(index.relation); }

    template<typename T>
        requires SupportsRelation<Repository, T>
    [[nodiscard]] const Repository& get_canonical_context(BindingIndex<T> index) const
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
    explicit Repository(size_t index, const Repository* parent = nullptr, urd::bit_width object_index_width = default_object_index_width) :
        m_parent(parent),
        m_root(m_parent ? m_parent->m_root : this),
        m_symbol_repository(m_parent ? &m_parent->m_symbol_repository : nullptr),
        m_relation_repository(m_parent ? &m_parent->m_relation_repository : nullptr, object_index_width),
        m_index(index)
    {
    }
    ~Repository() = default;
    Repository(const Repository& other) = delete;
    Repository& operator=(const Repository& other) = delete;
    Repository(Repository&& other) = delete;
    Repository& operator=(Repository&& other) = delete;

    /// The repository's number, as urd's views ask of a context.
    [[nodiscard]] std::size_t layer() const noexcept { return m_index; }
    [[nodiscard]] const auto& get_root() const noexcept { return *m_root; }
    [[nodiscard]] std::uint8_t get_object_index_width() const noexcept { return m_relation_repository.get_object_index_width(); }

    void clear()
    {
        m_symbol_repository.clear();
        m_relation_repository.clear();
    }
};

}  // namespace ygg::formalism

#endif
