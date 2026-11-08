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
#include "yggdrasil/formalism/relation_view.hpp"
#include "yggdrasil/formalism/relation_data.hpp"
#include "yggdrasil/formalism/relation_repository.hpp"
#include "yggdrasil/formalism/symbol_repository.hpp"

#include <algorithm>
#include <atomic>
#include <boost/mp11.hpp>
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <urd/config.hpp>
#include <urd/repr/containers/schema_rows.hpp>
#include <urd/repr/sequences/sequence.hpp>
#include <urd/repr/slots/slots.hpp>
#include <urd/store.hpp>
#include <urd/utils/family.hpp>
#include <utility>
#include <vector>

namespace ygg::formalism
{

/// A repository layer, the context of its views: symbols and relation bindings, over the layers of a parent, whose
/// records it finds too. A view's context is the layer that owns its record (its canonical context, found through
/// urd's owner), so that views of one record are equal wherever they were made. Each layer has its own number.
///
/// Every relation family F (a relation type: predicates, actions, ..., Schema) has rows, keyed {pool, row}. A family F
/// whose Relation<F> is among the layer's symbols (with Columns and RowSet) also has relations: a relation's rows go into
/// the layer's pool of its key (an index<F>), and the relation is {pool, columns, row set}. Rename keeps the pool and the
/// row set. Free relations are the Schema family: their key is their column types (schema(columns)).
///
/// Concurrent repository aliases support overlapping find, insert and lookups. Builders remain thread-confined; clear,
/// memory accounting, and destruction require quiescence. Parent layers must remain frozen and outlive their
/// descendants.
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
    /// The builders get_relation_builder hands out for family F, by their column widths: a builder handed back is
    /// reused only for equal widths, so its row storage stays.
    template<typename F>
    using BuilderSlots = urd::Store<urd::slots<urd::builder<Relation<F>>>, urd::keyed<urd::column_widths>>;
    template<typename F>
    using HasRelations = boost::mp11::mp_contains<SymbolTypes, Relation<F>>;
    template<typename... Fs>
    using BuilderFamily = urd::utils::Family<BuilderSlots, Fs...>;
    /// A pool key's records stay across clear(), and so do their pools.
    template<typename T>
    using KeysPools = boost::mp11::mp_contains<RelationTypes, T>;

    static urd::bit_width object_index_width(std::size_t num_objects, const Repository* parent)
    {
        if (num_objects > std::numeric_limits<ygg::uint_t>::max())
            throw std::invalid_argument("Repository: number of objects exceeds the object index domain.");
        auto width = urd::bit_width::for_bound(num_objects);
        if (parent)
            width.bits = std::max(width.bits, parent->relations().get_object_index_width());
        return width;
    }

    // ponytail: one counter per repository type, numbers are never reused.
    inline static std::atomic<std::size_t> s_next_layer { 0 };

    const Repository* m_parent;
    SymbolRepo m_symbols;
    RelationRepo m_relations;
    std::size_t m_layer;
    /// Scratch records of insert.
    urd::data<Schema> m_schema;
    urd::data<Columns> m_columns;
    urd::data<RowSet> m_row_set;
    /// The builder slots of each family with relations (none where the layer has no relations).
    mutable boost::mp11::mp_apply<BuilderFamily, boost::mp11::mp_copy_if<RelationTypes, HasRelations>> m_relation_builders;
    /// Whether `context` is this layer or one of its parents.
    [[nodiscard]] bool reads(const void* context) const noexcept
    {
        for (const auto* layer = this; layer != nullptr; layer = layer->m_parent)
            if (layer == context)
                return true;
        return false;
    }
    /// The relation `columns`, `pool` and `row_set` make.
    template<typename F>
    std::pair<urd::index_view<Relation<F>, Repository>, bool> intern_relation(urd::index<F> pool, std::span<const ColumnLayout> columns, urd::index<RowSet> row_set)
    {
        m_columns.values.set(columns.begin(), columns.end());
        auto relation = urd::data<Relation<F>> { {}, pool, intern(m_symbols.template get<Columns>(), m_columns).first, row_set };
        const auto [index, created] = intern(m_symbols.template get<Relation<F>>(), relation);
        return { view(index), created };
    }
    /// Interns the relation `source` reads (a builder's view, or a relation view of another layer): its rows into this
    /// layer's pool of `pool`, their sorted ids as its row set.
    template<typename F, typename V>
    std::pair<urd::index_view<Relation<F>, Repository>, bool> intern_rows(urd::index<F> pool, const V& source)
    {
        const auto columns = source.columns();
        auto& rows = m_relations.own_rows(pool, [&] { return m_relations.widths(columns | std::views::transform(&ColumnLayout::type)); });
        m_row_set.rows.clear();
        for (std::size_t i = 0; i < source.size(); ++i)
            m_row_set.rows.push_back(rows.insert(source.row(i)).first);
        std::ranges::sort(m_row_set.rows);
        return intern_relation(pool, columns, intern(m_symbols.template get<RowSet>(), m_row_set).first);
    }
    /// A relation's pool here: a free relation's is its column types, interned here whichever layer its builder or view
    /// came from; another family's is its key.
    urd::index<Schema> pool_of(urd::index<Schema>, std::span<const ColumnLayout> columns)
        requires SupportsSymbol<Repository, Schema>
    { return schema(columns); }
    template<typename F>
    static urd::index<F> pool_of(urd::index<F> pool, std::span<const ColumnLayout>)
    { return pool; }
    /// The column types of a pool: a free relation's Schema's, an object family's one object column per argument.
    [[nodiscard]] std::span<const TypeId> pool_types(urd::index<Schema> schema) const
        requires SupportsSymbol<Repository, Schema>
    {
        const auto& types = (*this)[schema].types;
        return { types.data(), types.size() };
    }
    template<typename F>
    [[nodiscard]] auto pool_types(urd::index<F> pool) const
    {
        const auto arity = m_relations.template get<F>().read(pool, [](const auto& rows) { return rows.column_widths().size(); }, std::size_t { 0 });
        return std::views::iota(std::size_t { 0 }, arity) | std::views::transform([](std::size_t) { return type_id<ObjectIndex>; });
    }

    Repository(const Repository* parent, urd::bit_width object_index_width) :
        m_parent(parent),
        m_symbols(parent ? &parent->m_symbols : nullptr),
        m_relations(parent ? &parent->m_relations : nullptr, object_index_width),
        m_layer(s_next_layer.fetch_add(1, std::memory_order_relaxed))
    {
    }

public:
    friend const Repository& get_repository(const Repository& repository URD_LIFETIMEBOUND) noexcept { return repository; }

    /// A layer whose bindings store whole object indices. Parent layers must remain frozen and outlive this one.
    explicit Repository(const Repository* parent = nullptr) : Repository(parent, default_object_index_width) {}
    /// A layer whose bindings store object indices below `num_objects` (at least as wide as the parent's).
    explicit Repository(std::size_t num_objects, const Repository* parent = nullptr) : Repository(parent, object_index_width(num_objects, parent)) {}
    Repository(const Repository& other) = delete;
    Repository& operator=(const Repository& other) = delete;
    Repository(Repository&& other) = delete;
    Repository& operator=(Repository&& other) = delete;
    ~Repository() = default;

    [[nodiscard]] const SymbolRepo& symbols() const noexcept { return m_symbols; }
    [[nodiscard]] SymbolRepo& symbols() noexcept { return m_symbols; }
    [[nodiscard]] const RelationRepo& relations() const noexcept { return m_relations; }
    [[nodiscard]] RelationRepo& relations() noexcept { return m_relations; }
    [[nodiscard]] const Repository* parent() const noexcept { return m_parent; }
    /// The layer's number, as urd's views ask of a context.
    [[nodiscard]] std::size_t layer() const noexcept { return m_layer; }

    /// Forgets this layer's own records and rows; the parent's stay. Views of the forgotten records
    /// dangle. Its pool keys (e.g. schemas) stay, and so their pools, emptied: a refill in any order finds each pool
    /// built alike and allocates nothing.
    void clear()
    {
        boost::mp11::mp_for_each<boost::mp11::mp_transform<std::type_identity, boost::mp11::mp_remove_if<SymbolTypes, KeysPools>>>(
            [this]<typename T>(std::type_identity<T>) { m_symbols.template get<T>().clear(); });
        m_relations.clear();
    }

    /**
     * Owners: the layer whose own records hold an index or a binding key, or nullptr where no layer does.
     */

    template<typename T>
        requires SupportsSymbol<Repository, T>
    [[nodiscard]] const Repository* owner(urd::index<T> index) const
    {
        if (!m_symbols.contains(index))
            return nullptr;
        const auto* store = &m_symbols.template get<T>().owner(index);
        const auto* layer = this;
        while (&layer->m_symbols.template get<T>() != store)
            layer = layer->m_parent;
        return layer;
    }

    template<typename T>
        requires SupportsRelation<Repository, T>
    [[nodiscard]] const Repository* owner(BindingIndex<T> key) const
    {
        const auto& relations = m_relations.template get<T>();
        if (key.relation.is_none() || !relations.contains(key.relation))
            return nullptr;
        const auto& rows = relations.at(key.relation);
        if ((key.row.value) >= rows.size())
            return nullptr;
        const auto* own = &rows.owner(key.row);
        const auto* layer = this;
        while (!layer->m_relations.template get<T>().read_own(key.relation, [own](const auto& held) { return &held == own; }, false))
            layer = layer->m_parent;
        return layer;
    }

    /**
     * Symbols: found in this layer and its parents.
     */

    template<typename T>
        requires SupportsSymbol<Repository, T>
    [[nodiscard]] std::optional<urd::index_view<T, Repository>> find(const urd::data<T>& builder) const
    {
        if (const auto index = m_symbols.template get<T>().find(builder); !index.is_none())
            return view(index);
        return std::nullopt;
    }

    /// Interns the record where no layer holds it; builder.index is set to its index.
    template<typename T>
        requires SupportsSymbol<Repository, T>
    std::pair<urd::index_view<T, Repository>, bool> insert(urd::data<T>& builder)
    {
        const auto [index, created] = intern(m_symbols.template get<T>(), builder);
        return { view(index), created };
    }

    template<typename T>
        requires SupportsSymbol<Repository, T>
    const urd::data<T>& operator[](urd::index<T> index) const
    {
        if (!m_symbols.contains(index))
            throw std::out_of_range("Symbol index not found in any repository layer.");
        return m_symbols[index];
    }

    /// An index as its view, through the layer that owns it (urd's resolves contract).
    template<typename T>
        requires SupportsSymbol<Repository, T>
    [[nodiscard]] urd::index_view<T, Repository> view(urd::index<T> index) const
    {
        if (const auto* layer = owner(index))
            return { index, layer };
        throw std::out_of_range("Symbol index not found in any repository layer.");
    }
    /// A record held outside the repository as its view here, which points at it (the record must outlive the view;
    /// never a temporary).
    template<typename T>
    [[nodiscard]] urd::borrowed_view<T, Repository> view(const urd::data<T>& record URD_LIFETIMEBOUND) const
    { return { &record, this }; }
    template<typename T>
    void view(const urd::data<T>&& record) const = delete;
    /// A builder as its view here, which points at it (the builder must outlive the view; never a temporary).
    template<typename T>
    [[nodiscard]] urd::borrowed_view<T, Repository, urd::builder<T>> view(const urd::builder<T>& builder URD_LIFETIMEBOUND) const
    { return { &builder, this }; }
    template<typename T>
    void view(const urd::builder<T>&& builder) const = delete;

    /**
     * Relation bindings: found in this layer and its parents.
     */

    template<typename T>
        requires SupportsRelation<Repository, T>
    [[nodiscard]] std::optional<BindingView<T>> find(const urd::data<RelationBinding<T, object_tag>>& builder) const
    {
        if (const auto row = m_relations.find(builder); !row.is_none())
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
        const auto [row, created] = m_relations.insert(builder);
        return { BindingView<T>(BindingIndex<T> { builder.relation, row }, this), created };
    }

    /// The binding's row: its relation and its objects.
    template<typename T>
        requires SupportsRelation<Repository, T>
    auto operator[](BindingIndex<T> key) const
    {
        const auto& rows = m_relations.template get<T>().at(key.relation);
        if ((key.row.value) >= rows.size())
            throw std::out_of_range("Relation binding index not found in any repository layer.");
        auto objects = rows.view(key.row).template as<urd::index<Object<object_tag>>>();
        return RelationBindingRow<T, decltype(objects)> { key.relation, objects };
    }

    /// The columns of a pool's rows, labeled by position.
    template<typename F>
        requires SupportsRelation<Repository, F>
    [[nodiscard]] std::vector<ColumnLayout> layout(urd::index<F> pool) const
    {
        auto result = std::vector<ColumnLayout>();
        for (const TypeId type : pool_types(pool))
            result.push_back(ColumnLayout { urd::index<Column>(static_cast<std::uint32_t>(result.size())), type });
        return result;
    }

    /**
     * Relations of each family F whose Relation<F> is among the symbols: interned as {pool, columns, row set}.
     */

    /// The key of free relations of these columns' types: their Schema, interned here.
    urd::index<Schema> schema(std::span<const ColumnLayout> columns)
        requires SupportsSymbol<Repository, Schema>
    {
        m_schema.types.clear();
        for (const auto& column : columns)
            m_schema.types.push_back(column.type);
        return intern(m_symbols.template get<Schema>(), m_schema).first;
    }
    /// Interns the builder's relation: its rows into this layer's pool of its key, their sorted ids as its row set.
    template<typename F>
        requires SupportsSymbol<Repository, Relation<F>>
    std::pair<urd::index_view<Relation<F>, Repository>, bool> insert(const urd::builder<Relation<F>>& builder)
    { return intern_rows(pool_of(builder.pool(), builder.columns()), view(builder)); }
    /// A builder of `pool` with these columns (validated), shared as tyr's state builders are: it comes from this
    /// layer's slots, keyed by its column widths, where it goes back with its last owner, keeping its row storage.
    /// insert() interns it. The layer must outlive its builders.
    template<typename F>
        requires SupportsSymbol<Repository, Relation<F>>
    [[nodiscard]] std::shared_ptr<urd::builder<Relation<F>>> get_relation_builder(urd::index<F> pool, std::span<const ColumnLayout> columns) const
    {
        auto builder = m_relation_builders.template get<F>().acquire_shared(m_relations.widths(columns | std::views::transform(&ColumnLayout::type)), pool, columns);
        builder->initialize(pool, columns);  // a builder handed back holds its last pool, columns and rows
        return builder;
    }
    /// A relation of another layer or repository interned here; one this layer reads is itself.
    template<typename F, typename C>
        requires SupportsSymbol<Repository, Relation<F>>
    std::pair<urd::index_view<Relation<F>, Repository>, bool> copy(urd::index_view<Relation<F>, C> source)
    {
        if (reads(&source.context()))
            return { view(source.index()), false };
        return intern_rows(pool_of(source.data().pool, source.columns()), source);
    }
    /// The relation relabeled: the same pool and row set (so the same rows), new columns of the same types and
    /// distinct labels.
    template<typename F, typename C>
        requires SupportsSymbol<Repository, Relation<F>>
    urd::index_view<Relation<F>, Repository> rename(urd::index_view<Relation<F>, C> source, std::span<const ColumnLayout> columns)
    {
        if (!reads(&source.context()))
            throw std::invalid_argument("Repository: rename requires a source in this repository.");
        if (!std::ranges::equal(source.columns(), columns, {}, &ColumnLayout::type, &ColumnLayout::type))
            throw std::invalid_argument("Repository: a rename keeps the column types.");
        for (auto it = columns.begin(); it != columns.end(); ++it)
            if (std::ranges::find(columns.begin(), it, it->label, &ColumnLayout::label) != it)
                throw std::invalid_argument("Repository: a rename gives distinct labels.");
        const auto& record = source.data();
        return intern_relation(record.pool, columns, record.row_set).first;
    }
    /// A binding key as its view, through the layer that owns the binding.
    template<typename T>
        requires SupportsRelation<Repository, T>
    [[nodiscard]] BindingView<T> view(BindingIndex<T> key) const
    {
        if (const auto* layer = owner(key))
            return { key, layer };
        throw std::out_of_range("Relation binding index not found in any repository layer.");
    }
};

}  // namespace ygg::formalism

#endif
