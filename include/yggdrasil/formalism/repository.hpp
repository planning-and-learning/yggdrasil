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

#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/membership.hpp"
#include "yggdrasil/formalism/relation_data.hpp"
#include "yggdrasil/formalism/relation_repository.hpp"
#include "yggdrasil/formalism/relation_view.hpp"
#include "yggdrasil/formalism/row_view.hpp"
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

/// A repository layer, the context of its views: symbols and rows, over the layers of a parent, whose records it finds
/// too. A view's context is the layer that owns its record (its canonical context, found through urd's owner), so that
/// views of one record are equal wherever they were made. Each layer has its own number.
///
/// Every relation family F (HasRows: predicates, actions, ..., Schema) has rows Row<F>, keyed {pool, row}. A family F
/// with relations (HasRelations: Relation<F> among the layer's symbols, with Columns and RowSet<F>) also has relations:
/// a relation's rows go into the layer's pool of its key (an index<F>), and the relation is {pool, columns, row set}.
/// Rename keeps the pool and the row set. Free relations are the Schema family: their key is their column types,
/// interned at insert.
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
    /// The builders get_relation_builder hands out for family F, by their column widths: a builder handed back is
    /// reused only for equal widths, so its row storage stays.
    template<typename F>
    using BuilderSlots = urd::Store<urd::slots<urd::builder<Relation<F>>>, urd::keyed<urd::column_widths>>;
    template<typename F>
    using RelationFamily = boost::mp11::mp_contains<SymbolTypes, Relation<F>>;
    template<typename... Fs>
    using BuilderFamily = urd::utils::Family<BuilderSlots, Fs...>;
    template<typename F>
    using RowSetRecord = urd::data<RowSet<F>>;
    template<typename... Fs>
    using RowSetFamily = urd::utils::Family<RowSetRecord, Fs...>;
    using RelationFamilies = boost::mp11::mp_copy_if<RelationTypes, RelationFamily>;
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

    const Repository* m_parent;
    SymbolRepo m_symbols;
    RelationRepo m_relations;
    std::size_t m_layer;
    /// Scratch records of insert.
    urd::data<Schema> m_schema;
    urd::data<Columns> m_columns;
    boost::mp11::mp_apply<RowSetFamily, RelationFamilies> m_row_sets;
    /// The builder slots of each family with relations (none where the layer has no relations).
    mutable boost::mp11::mp_apply<BuilderFamily, RelationFamilies> m_relation_builders;
    /// Whether `context` is this layer or one of its parents.
    [[nodiscard]] bool reads(const void* context) const noexcept
    {
        for (const auto* layer = this; layer != nullptr; layer = layer->m_parent)
            if (layer == context)
                return true;
        return false;
    }
    /// The relation `pool`, `columns` and `row_set` make. Its Columns record is made here only, of the pool's types: the
    /// columns' types must be the pool's (std::invalid_argument otherwise).
    template<typename F>
    std::pair<urd::index_view<Relation<F>, Repository>, bool> intern_relation(urd::index<F> pool, std::span<const ColumnLayout> columns, urd::index<RowSet<F>> row_set)
    {
        if (!std::ranges::equal(m_relations.types(pool), columns, {}, {}, &ColumnLayout::type))
            throw std::invalid_argument("Repository: the columns' types are not the pool's.");
        m_columns.values.set(columns.begin(), columns.end());
        auto relation = urd::data<Relation<F>> { {}, pool, intern(m_symbols.template get<Columns>(), m_columns).first, row_set };
        const auto [index, created] = intern(m_symbols.template get<Relation<F>>(), relation);
        return { view(index), created };
    }
    /// Interns the relation `source` reads (a builder's view, or a relation view of another layer): its rows into this
    /// layer's pool of `pool`, whose column types must be the source's, their sorted ids as its row set.
    template<typename F, typename V>
    std::pair<urd::index_view<Relation<F>, Repository>, bool> intern_rows(urd::index<F> pool, const V& source)
    {
        const auto columns = source.columns();
        auto& rows = m_relations.own_rows(pool, columns | std::views::transform(&ColumnLayout::type));
        auto& row_set = m_row_sets.template get<F>();
        row_set.rows.clear();
        for (std::size_t i = 0; i < source.size(); ++i)
            row_set.rows.push_back(rows.insert(source.row(i).get_values()).first);
        std::ranges::sort(row_set.rows);
        return intern_relation(pool, columns, intern(m_symbols.template get<RowSet<F>>(), row_set).first);
    }
    /// The key of free relations of these columns' types: their Schema, interned here.
    urd::index<Schema> schema(std::span<const ColumnLayout> columns)
    {
        m_schema.types.clear();
        for (const auto& column : columns)
            m_schema.types.push_back(column.type);
        return intern(m_symbols.template get<Schema>(), m_schema).first;
    }

    Repository(const Repository* parent, urd::bit_width object_index_width) :
        m_parent(parent),
        m_symbols(parent ? &parent->m_symbols : nullptr),
        m_relations(parent ? &parent->m_relations : nullptr, object_index_width),
        m_layer(next_layer())
    {
    }

public:
    friend const Repository& get_repository(const Repository& repository URD_LIFETIMEBOUND) noexcept { return repository; }

    /// A layer whose rows store whole object indices. Parent layers must remain frozen and outlive this one.
    explicit Repository(const Repository* parent = nullptr) : Repository(parent, default_object_index_width) {}
    /// A layer whose rows store object indices below `num_objects` (at least as wide as the parent's).
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
     * Owners: the layer whose own records hold an index or a row key, or nullptr where no layer does.
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

    template<typename F>
        requires HasRows<Repository, F>
    [[nodiscard]] const Repository* owner(RowIndex<F> key) const
    {
        const auto& relations = m_relations.template get<F>();
        if (key.relation.is_none() || !relations.contains(key.relation))
            return nullptr;
        const auto& rows = relations.at(key.relation);
        if ((key.row.value) >= rows.size())
            return nullptr;
        const auto* own = &rows.owner(key.row);
        const auto* layer = this;
        while (!layer->m_relations.template get<F>().read_own(key.relation, [own](const auto& held) { return &held == own; }, false))
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
     * Rows of each family F with rows: found in this layer and its parents.
     */

    template<typename F>
        requires HasRows<Repository, F>
    [[nodiscard]] std::optional<RowView<F, Repository>> find(const urd::data<Row<F>>& row) const
    {
        if (const auto id = m_relations.find(row); !id.is_none())
            return view(RowIndex<F> { row.relation, id });
        return std::nullopt;
    }

    /// Stores the row in this layer where no layer holds it. Its pool's columns are objects, one per value (checked
    /// against the pool's types). A free relation's rows go in through its relation (insert(builder)).
    template<typename F>
        requires HasRows<Repository, F> && (!std::same_as<F, Schema>)
    std::pair<RowView<F, Repository>, bool> insert(const urd::data<Row<F>>& row)
    {
        if (auto found = find(row))
            return { *found, false };
        const auto types = std::views::iota(std::size_t { 0 }, row.values.size()) | std::views::transform([](std::size_t) { return type_id<urd::index<Object<object_tag>>>; });
        const auto [id, created] = m_relations.insert(row, types);
        return { RowView<F, Repository>(RowIndex<F> { row.relation, id }, this), created };
    }

    /// The stored row of a key, as its views read it.
    template<typename F>
        requires HasRows<Repository, F>
    auto operator[](RowIndex<F> key) const
    {
        const auto& rows = m_relations.template get<F>().at(key.relation);
        if ((key.row.value) >= rows.size())
            throw std::out_of_range("Row index not found in any repository layer.");
        return rows.view(key.row);
    }

    /// A row key as its view, through the layer that owns the row.
    template<typename F>
        requires HasRows<Repository, F>
    [[nodiscard]] RowView<F, Repository> view(RowIndex<F> key) const
    {
        if (const auto* layer = owner(key))
            return { key, layer };
        throw std::out_of_range("Row index not found in any repository layer.");
    }

    /**
     * Relations of each family F with relations: interned as {pool, columns, row set}.
     */

    /// Interns a free relation's builder: its key is its column types (their Schema, interned here).
    std::pair<urd::index_view<Relation<Schema>, Repository>, bool> insert(const urd::builder<Relation<Schema>>& builder)
        requires HasRelations<Repository, Schema>
    { return intern_rows(schema(builder.columns()), view(builder)); }
    /// Interns the builder's relation: its rows into this layer's pool of `pool`, their sorted ids as its row set.
    template<typename F>
        requires HasRelations<Repository, F>
    std::pair<urd::index_view<Relation<F>, Repository>, bool> insert(urd::index<F> pool, const urd::builder<Relation<F>>& builder)
    { return intern_rows(pool, view(builder)); }
    /// A builder of family F with these columns (validated), shared as tyr's state builders are: it comes from this
    /// layer's slots, keyed by its column widths, where it goes back with its last owner, keeping its row storage.
    /// insert() interns it. The layer must outlive its builders.
    template<typename F>
        requires HasRelations<Repository, F>
    [[nodiscard]] std::shared_ptr<urd::builder<Relation<F>>> get_relation_builder(std::span<const ColumnLayout> columns) const
    {
        auto builder = m_relation_builders.template get<F>().acquire_shared(m_relations.widths(columns | std::views::transform(&ColumnLayout::type)), columns);
        builder->initialize(columns);  // a builder handed back holds its last columns and rows
        return builder;
    }
    /// A free relation of another layer or repository interned here (its key its column types); one this layer reads is
    /// itself.
    template<typename C>
        requires HasRelations<Repository, Schema>
    std::pair<urd::index_view<Relation<Schema>, Repository>, bool> copy(urd::index_view<Relation<Schema>, C> source)
    {
        if (reads(&source.context()))
            return { view(source.index()), false };
        return intern_rows(schema(source.columns()), source);
    }
    /// A relation of another layer or repository interned here, into the pool of `pool`; one this layer reads is
    /// itself.
    template<typename F, typename C>
        requires HasRelations<Repository, F>
    std::pair<urd::index_view<Relation<F>, Repository>, bool> copy(urd::index<F> pool, urd::index_view<Relation<F>, C> source)
    {
        if (reads(&source.context()))
            return { view(source.index()), false };
        return intern_rows(pool, source);
    }
    /// The relation relabeled: the same pool and row set (so the same rows), new columns of the pool's types and
    /// distinct labels.
    template<typename F, typename C>
        requires HasRelations<Repository, F>
    urd::index_view<Relation<F>, Repository> rename(urd::index_view<Relation<F>, C> source, std::span<const ColumnLayout> columns)
    {
        if (!reads(&source.context()))
            throw std::invalid_argument("Repository: rename requires a source in this repository.");
        for (auto it = columns.begin(); it != columns.end(); ++it)
            if (std::ranges::find(columns.begin(), it, it->label, &ColumnLayout::label) != it)
                throw std::invalid_argument("Repository: a rename gives distinct labels.");
        const auto& record = source.data();
        return intern_relation(record.pool, columns, record.row_set).first;
    }
};

}  // namespace ygg::formalism

#endif
