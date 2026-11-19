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

#include "yggdrasil/formalism/builder.hpp"
#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/interning.hpp"
#include "yggdrasil/formalism/membership.hpp"
#include "yggdrasil/formalism/relation_data.hpp"
#include "yggdrasil/formalism/relation_view.hpp"
#include "yggdrasil/formalism/row_view.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <urd/config.hpp>
#include <urd/elements/packed_row.hpp>
#include <urd/elements/view.hpp>
#include <utility>
#include <vector>

namespace ygg::formalism
{

/// A repository layer, the context of its views: symbols and rows, over the layers of a parent, whose records it finds
/// too. A view's context is the layer that owns its record (its canonical context, found through urd's owner), so that
/// views of one record are equal wherever they were made. Each layer has its own number.
///
/// Every relation family F (HasRows: predicates, actions, ..., Schema) has rows Row<F>, keyed {pool, row}. A family F
/// with relations (HasRelations: Relation<F> among the layer's symbols, with Columns) also has relations: a
/// relation's rows go into the layer's pool of its key (an index<F>), and the relation is {pool, columns, row ids}.
/// Rename keeps the pool and the rows. Free relations are the Schema family: their key is their column types,
/// interned at insert.
///
/// Concurrent repository aliases support overlapping find, insert and lookups. Builders remain thread-confined; clear,
/// memory accounting, and destruction require quiescence. Parent layers must remain frozen and outlive their
/// descendants.
///
/// This header declares the repository; its stores (the symbol and relation repositories, the builders) are behind a
/// pointer to its Impl, which repository_impl.hpp defines with every member. A library includes repository_impl.hpp in
/// one source and instantiates its repositories' members there (repository_instantiations.hpp); its other sources
/// include this header only, and instantiate none of the stores.
template<typename SymbolRepo, typename RelationRepo>
class Repository
{
public:
    using SymbolTypes = typename detail::symbol_repository_traits<SymbolRepo>::SymbolTypes;
    using RelationTypes = typename detail::relation_repository_traits<RelationRepo>::RelationTypes;
    /// What a stored row's values read as (field): decoded from its pool's column widths.
    using StoredRow = urd::packed_row<detail::relation_repository_traits<RelationRepo>::thread_safe>;
    static_assert(RowValues<StoredRow>);
    /// Whether finds, inserts and lookups may overlap (its symbols and its relations concurrent).
    static constexpr bool thread_safe =
        detail::symbol_repository_traits<SymbolRepo>::thread_safe && detail::relation_repository_traits<RelationRepo>::thread_safe;

    /// The stores: defined in repository_impl.hpp.
    struct Impl;

    friend const Repository& get_repository(const Repository& repository URD_LIFETIMEBOUND) noexcept { return repository; }

    /// A layer whose rows store whole object indices. Parent layers must remain frozen and outlive this one.
    explicit Repository(const Repository* parent = nullptr);
    /// A layer whose rows store object indices below `num_objects` (at least as wide as the parent's).
    explicit Repository(std::size_t num_objects, const Repository* parent = nullptr);
    Repository(const Repository& other) = delete;
    Repository& operator=(const Repository& other) = delete;
    Repository(Repository&& other) = delete;
    Repository& operator=(Repository&& other) = delete;
    ~Repository();

    [[nodiscard]] const Repository* parent() const noexcept { return m_parent; }

    /// The layer's number, as urd's views ask of a context.
    [[nodiscard]] std::size_t layer() const noexcept { return m_layer; }

    /// Forgets this layer's own records and rows; the parent's stay. Views of the forgotten records
    /// dangle. Its pool keys (e.g. predicates) stay, with their pools' declarations (Schemas), and so their pools, emptied: a refill in any order finds each
    /// pool built alike and allocates nothing.
    void clear();
    /// Forgets every record and row of this layer, its pool keys and their pools too; the parent's stay.
    void clear_all();

    /**
     * Owners: the layer whose own records hold an index or a row key, or nullptr where no layer does.
     */

    template<typename T>
        requires SupportsSymbol<Repository<SymbolRepo, RelationRepo>, T>
    [[nodiscard]] const Repository* owner(urd::index<T> index) const
    { return do_owner<T>(index); }

    template<typename F>
        requires HasRows<Repository<SymbolRepo, RelationRepo>, F>
    [[nodiscard]] const Repository* owner(RowIndex<F> key) const
    { return do_owner<F>(key); }

    /**
     * Symbols: found in this layer and its parents.
     */

    template<typename T>
        requires SupportsSymbol<Repository<SymbolRepo, RelationRepo>, T>
    [[nodiscard]] std::optional<urd::index_view<T, Repository>> find(const urd::data<T>& record) const
    { return do_find<T>(record); }

    /// Interns the record where no layer holds it; record.index is set to its index. A key of row pools (a predicate,
    /// an action, ...) has its pool declared too, with the column types its view gives (a key family's view has
    /// get_column_types(): its rows' column types); declared on every insert, so a row may follow whichever thread
    /// interned it.
    template<typename T>
        requires SupportsSymbol<Repository<SymbolRepo, RelationRepo>, T>
    std::pair<urd::index_view<T, Repository>, bool> insert(urd::data<T>& record)
    { return do_insert<T>(record); }

    /// Interns a builder through the scratch records of a builder storage (its scratch<T>(), each worker's own; no
    /// allocation once they have grown; the records of the repositories nested in this one from the same storage): its built children and records first (each prepared, as
    /// the repository's domain makes it canonical, and inserted), interned children kept as they are, then its record,
    /// written into a scratch record, prepared and inserted. The view of its record, and whether it was new. A child
    /// interned in another repository of this kind, or a child of another repository not interned there, throws
    /// std::invalid_argument.
    template<typename T, IsBuilderStorage Storage>
    decltype(auto) insert(const urd::builder<T, Repository>& built, Storage& storage)
    { return detail::insert_child(*this, storage, built); }

    /// Finds the record a builder writes, inserting nothing: its children looked up the same way (a child not found
    /// names no record here, so then nothing is found; a record of its own is prepared in place), then its record.
    template<typename T, IsBuilderStorage Storage>
    found_t<Repository, T> find(const urd::builder<T, Repository>& built, Storage& storage)
    { return detail::find_child(*this, storage, built); }

    /// Interns a view of a record of its kinds as it reads it (what a child resolves to): an interned one is checked
    /// to be its own, a builder's is interned as that builder.
    template<ViewThrough<Repository> V, IsBuilderStorage Storage>
    decltype(auto) insert(const V& view, Storage& storage)
    { return detail::insert_view(*this, storage, view); }

    /// Finds the record a view reads, inserting nothing, likewise.
    template<ViewThrough<Repository> V, IsBuilderStorage Storage>
    auto find(const V& view, Storage& storage)
    { return detail::find_view(*this, storage, view); }

    /// The column types of the pool `relation`, as declared (std::out_of_range where no layer declared it).
    template<typename F>
        requires HasRows<Repository<SymbolRepo, RelationRepo>, F>
    [[nodiscard]] std::span<const TypeId> types(urd::index<F> relation) const
    { return do_types<F>(relation); }

    template<typename T>
        requires SupportsSymbol<Repository<SymbolRepo, RelationRepo>, T>
    const urd::data<T>& operator[](urd::index<T> index) const
    { return do_at<T>(index); }

    /// Whether some layer holds the index.
    template<typename T>
        requires SupportsSymbol<Repository<SymbolRepo, RelationRepo>, T>
    [[nodiscard]] bool contains(urd::index<T> index) const
    { return do_contains<T>(index); }

    /// The number of records of type T, along the layers.
    template<typename T>
        requires SupportsSymbol<Repository<SymbolRepo, RelationRepo>, T>
    [[nodiscard]] std::size_t size() const
    { return do_size<T>(); }

    /// The memory of this layer's records of type T, in bytes.
    template<typename T>
        requires SupportsSymbol<Repository<SymbolRepo, RelationRepo>, T>
    [[nodiscard]] std::size_t memory_usage() const
    { return do_memory_usage<T>(); }

    /// An index as its view, through the layer that owns it (urd's resolves contract).
    template<typename T>
        requires SupportsSymbol<Repository<SymbolRepo, RelationRepo>, T>
    [[nodiscard]] urd::index_view<T, Repository> view(urd::index<T> index) const
    { return do_view<T>(index); }

    /// A record held outside the repository as its view here, which points at it (the record must outlive the view;
    /// never a temporary).
    template<typename T>
    [[nodiscard]] urd::borrowed_view<T, Repository> view(const urd::data<T>& record URD_LIFETIMEBOUND) const
    { return { &record, this }; }

    template<typename T>
    void view(const urd::data<T>&& record) const = delete;

    /// A builder as its view here, which points at it (the builder must outlive the view; never a temporary).
    template<typename T>
    [[nodiscard]] urd::borrowed_view<T, Repository, urd::builder<T, Repository>> view(const urd::builder<T, Repository>& builder URD_LIFETIMEBOUND) const
    { return { &builder, this }; }

    template<typename T>
    void view(const urd::builder<T, Repository>&& builder) const = delete;

    /**
     * Rows of each family F with rows: found in this layer and its parents.
     */

    template<typename F>
        requires HasRows<Repository<SymbolRepo, RelationRepo>, F>
    [[nodiscard]] std::optional<RowView<F, Repository>> find(const urd::data<Row<F>>& row) const
    { return do_find<F>(row); }

    /// Stores the row in this layer where no layer holds it, checked against its pool's Schema (declared with its key;
    /// a free relation's pool by schema()).
    template<typename F>
        requires HasRows<Repository<SymbolRepo, RelationRepo>, F>
    std::pair<RowView<F, Repository>, bool> insert(const urd::data<Row<F>>& row)
    { return do_insert<F>(row); }

    /// The id of the row of these values in the pool `relation`, in whichever layer holds it, or none; no view is made.
    /// The values as they are held: contiguous, or a stored row's.
    template<typename F>
        requires HasRows<Repository<SymbolRepo, RelationRepo>, F>
    [[nodiscard]] urd::index<Row<F>> find_row(urd::index<F> relation, std::span<const Value> values) const
    { return do_find_row<F>(relation, values); }

    template<typename F>
        requires HasRows<Repository<SymbolRepo, RelationRepo>, F>
    [[nodiscard]] urd::index<Row<F>> find_row(urd::index<F> relation, const StoredRow& values) const
    { return do_find_row<F>(relation, values); }

    /// The number of rows of the pool `relation`, along the layers.
    template<typename F>
        requires HasRows<Repository<SymbolRepo, RelationRepo>, F>
    [[nodiscard]] std::size_t size(urd::index<F> relation) const
    { return do_size<F>(relation); }

    /// The memory of family F's pools of rows in this layer, in bytes.
    template<typename F>
        requires HasRows<Repository<SymbolRepo, RelationRepo>, F>
    [[nodiscard]] std::size_t row_memory_usage() const
    { return do_row_memory_usage<F>(); }

    /// Stores the row in this layer where no layer holds it: its id in its pool, and whether it was stored here; no view
    /// is made (insert(data<Row<F>>) makes one).
    template<typename F>
        requires HasRows<Repository<SymbolRepo, RelationRepo>, F>
    std::pair<urd::index<Row<F>>, bool> insert_row(const urd::data<Row<F>>& row)
    { return do_insert_row<F>(row.relation, std::span<const Value>(row.values.data(), row.values.size())); }

    /// The row of these values in the pool `relation`, stored as insert_row(data<Row<F>>) stores one. The values as they
    /// are held: contiguous, or a stored row's.
    template<typename F>
        requires HasRows<Repository<SymbolRepo, RelationRepo>, F>
    std::pair<urd::index<Row<F>>, bool> insert_row(urd::index<F> relation, std::span<const Value> values)
    { return do_insert_row<F>(relation, values); }

    template<typename F>
        requires HasRows<Repository<SymbolRepo, RelationRepo>, F>
    std::pair<urd::index<Row<F>>, bool> insert_row(urd::index<F> relation, const StoredRow& values)
    { return do_insert_row<F>(relation, values); }

    /// A Schema's record and view, through the layer that owns it: a relation's pool of free relations resolves to it.
    const urd::data<Schema>& operator[](urd::index<Schema> index) const;
    [[nodiscard]] urd::index_view<Schema, Repository> view(urd::index<Schema> index) const;

    /// A field of the row of a key, as its views read it (urd::ReadsFieldsByKey): a row is stored packed, so there is no
    /// data<Row<F>> to hand out; its relation is the view of the key's, its values are decoded from its pool
    /// (RowValues).
    template<auto Field, typename F>
        requires HasRows<Repository<SymbolRepo, RelationRepo>, F>
    [[nodiscard]] auto field(RowIndex<F> key) const
    {
        if constexpr (std::same_as<decltype(Field), decltype(&urd::data<Row<F>>::relation)>)
            return urd::resolve(key.relation, *this);
        else
        {
            static_assert(Field == &urd::data<Row<F>>::values, "a row's fields are its relation and its values");
            return do_at<F>(key);
        }
    }

    /// The values of the row of a key, decoded from its pool's column widths: what its view's get_values() gives.
    template<typename F>
        requires HasRows<Repository<SymbolRepo, RelationRepo>, F>
    [[nodiscard]] StoredRow values(RowIndex<F> key) const
    { return do_at<F>(key); }

    /// A row key as its view, through the layer that owns the row.
    template<typename F>
        requires HasRows<Repository<SymbolRepo, RelationRepo>, F>
    [[nodiscard]] RowView<F, Repository> view(RowIndex<F> key) const
    { return do_view<F>(key); }

    /**
     * Relations of each family F with relations: records {pool, columns, row ids}, built by their users (rows interned
     * with insert(data<Row<F>>), columns with insert(data<Columns>)) and interned as any record.
     */

    /// The pool of free relations of these column types: their Schema, interned here, its pool declared.
    urd::index<Schema> schema(std::span<const TypeId> types)
        requires HasRows<Repository<SymbolRepo, RelationRepo>, Schema>
    { return do_schema(types); }

    /// A Schema's record as schema(types) interns it: the view of the pool of free relations of its types.
    urd::index_view<Schema, Repository> insert(const urd::data<Schema>& schema)
        requires HasRows<Repository<SymbolRepo, RelationRepo>, Schema>
    { return view(do_schema(std::span<const TypeId>(schema.types.data(), schema.types.size()))); }

    /// The pool of free relations of these columns' types.
    urd::index<Schema> schema(std::span<const ColumnLayout> columns)
        requires HasRows<Repository<SymbolRepo, RelationRepo>, Schema>
    { return do_schema(columns); }

    /// Interns the relation where no layer holds it, as given: its rows canonical (in tuple order: by their values, lexicographically; and
    /// unique: ygg::canonicalize, which ygg::formalism::insert applies first, and which a builder keeps). Its pool must be declared
    /// (std::out_of_range otherwise), its columns' types the pool's and their labels distinct (std::invalid_argument).
    template<typename F>
        requires HasRelations<Repository<SymbolRepo, RelationRepo>, F>
    std::pair<urd::index_view<Relation<F>, Repository>, bool> insert(urd::data<Relation<F>>& relation)
    { return do_insert<F>(relation); }

private:
    // What a library instantiates: the constrained members above, unconstrained, which they forward to. Clang names an
    // instantiation's requires-clause in its symbol and g++ does not, so only unconstrained symbols link between code
    // built by either.
    template<typename T>
    [[nodiscard]] const Repository* do_owner(urd::index<T> index) const;
    template<typename F>
    [[nodiscard]] const Repository* do_owner(RowIndex<F> key) const;
    template<typename T>
    [[nodiscard]] std::optional<urd::index_view<T, Repository>> do_find(const urd::data<T>& record) const;
    template<typename T>
    std::pair<urd::index_view<T, Repository>, bool> do_insert(urd::data<T>& record);
    template<typename F>
    [[nodiscard]] std::span<const TypeId> do_types(urd::index<F> relation) const;
    template<typename T>
    [[nodiscard]] const urd::data<T>& do_at(urd::index<T> index) const;
    template<typename T>
    [[nodiscard]] bool do_contains(urd::index<T> index) const;
    template<typename T>
    [[nodiscard]] std::size_t do_size() const;
    template<typename T>
    [[nodiscard]] std::size_t do_memory_usage() const;
    template<typename T>
    [[nodiscard]] urd::index_view<T, Repository> do_view(urd::index<T> index) const;
    template<typename F>
    [[nodiscard]] std::optional<RowView<F, Repository>> do_find(const urd::data<Row<F>>& row) const;
    template<typename F>
    std::pair<RowView<F, Repository>, bool> do_insert(const urd::data<Row<F>>& row);
    template<typename F>
    [[nodiscard]] urd::index<Row<F>> do_find_row(urd::index<F> relation, std::span<const Value> values) const;
    template<typename F>
    [[nodiscard]] urd::index<Row<F>> do_find_row(urd::index<F> relation, const StoredRow& values) const;
    template<typename F>
    [[nodiscard]] std::size_t do_size(urd::index<F> relation) const;
    template<typename F>
    [[nodiscard]] std::size_t do_row_memory_usage() const;
    template<typename F>
    std::pair<urd::index<Row<F>>, bool> do_insert_row(urd::index<F> relation, std::span<const Value> values);
    template<typename F>
    std::pair<urd::index<Row<F>>, bool> do_insert_row(urd::index<F> relation, const StoredRow& values);
    template<typename F>
    [[nodiscard]] StoredRow do_at(RowIndex<F> key) const;
    template<typename F>
    [[nodiscard]] RowView<F, Repository> do_view(RowIndex<F> key) const;
    template<typename F>
    std::pair<urd::index_view<Relation<F>, Repository>, bool> do_insert(urd::data<Relation<F>>& relation);
    urd::index<Schema> do_schema(std::span<const TypeId> types);
    urd::index<Schema> do_schema(std::span<const ColumnLayout> columns);

    const Repository* m_parent;
    std::size_t m_layer;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace ygg::formalism

#endif
