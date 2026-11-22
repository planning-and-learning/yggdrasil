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
#include "yggdrasil/formalism/keyed.hpp"
#include "yggdrasil/formalism/relation_data.hpp"
#include "yggdrasil/formalism/relation_view.hpp"
#include "yggdrasil/formalism/row_view.hpp"

#include <array>
#include <boost/mp11.hpp>
#include <cstddef>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <tuple>
#include <urd/config.hpp>
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
class Repository : public KeyedRepository<Repository<SymbolRepo, RelationRepo>>
{
public:
    using SymbolTypes = typename detail::symbol_repository_traits<SymbolRepo>::SymbolTypes;
    using RelationTypes = typename detail::relation_repository_traits<RelationRepo>::RelationTypes;
    /// The keys it resolves (operator[]): its symbols' and its relation repository's.
    using KeyTypes = boost::mp11::mp_append<typename detail::symbol_repository_traits<SymbolRepo>::KeyTypes,
                                            typename detail::relation_repository_traits<RelationRepo>::KeyTypes>;
    /// Its own parts that resolve keys (key_parts), behind its Impl: its symbol repository, then its relation
    /// repository (own_parts()). It nests no repository.
    using OwnParts = std::tuple<HeldPart<const Repository,
                                         const typename detail::symbol_repository_traits<SymbolRepo>::Store,
                                         typename detail::symbol_repository_traits<SymbolRepo>::KeyTypes>,
                                HeldPart<const Repository,
                                         const typename detail::relation_repository_traits<RelationRepo>::Store,
                                         typename detail::relation_repository_traits<RelationRepo>::KeyTypes>>;
    /// Its relation repository as a part records are written into: its pools of rows made on demand, with the widths of
    /// their relation symbols' column types (read through this repository); defined in repository_impl.hpp.
    struct RelationSlots;
    /// Its own parts that records are written into, in the order of OwnParts (own_writable_parts()): its symbol
    /// repository, then its relation repository's slots.
    using OwnWritableParts = std::tuple<HeldPart<Repository,
                                                 typename detail::symbol_repository_traits<SymbolRepo>::Store,
                                                 typename detail::symbol_repository_traits<SymbolRepo>::KeyTypes>,
                                        HeldPart<Repository, RelationSlots, typename detail::relation_repository_traits<RelationRepo>::KeyTypes>>;
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

    /// Whether a key of `source` names the same record here (urd::Sees): `source` is this layer or one it lies over.
    [[nodiscard]] bool sees(const Repository& source) const noexcept
    {
        for (const Repository* layer = this; layer != nullptr; layer = layer->m_parent)
        {
            if (layer == &source)
                return true;
        }
        return false;
    }

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
    [[nodiscard]] const Repository* owner(urd::key<F, Row<F>> key) const
    { return do_owner<F>(key); }

    /**
     * Symbols: found in this layer and its parents.
     */

    /// Every record (a symbol, a Schema, a row under its relation) is stored and found by the one insert(data<T>&) and
    /// find(const data<T>&) of KeyedRepository, through the part that holds its kind; its record's key written into it.
    using KeyedRepository<Repository>::insert;
    using KeyedRepository<Repository>::find;

    /// Interns a builder through the scratch records of a builder storage (its scratch<T>(), each worker's own; no
    /// allocation once they have grown; the records of the repositories nested in this one from the same storage): its built children and records first (each
    /// prepared, as the repository's domain makes it canonical, and inserted), interned children kept as they are, then its record, written into a scratch
    /// record, prepared and inserted. The view of its record, and whether it was new. A child interned in another repository of this kind, or a child of
    /// another repository not interned there, throws std::invalid_argument.
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

    /// The column types of the pool `relation`: those its relation symbol's record stores (column_types), read in place
    /// (std::out_of_range where no layer holds the relation symbol).
    template<typename F>
        requires HasRows<Repository<SymbolRepo, RelationRepo>, F>
    [[nodiscard]] std::span<const TypeId> types(urd::index<F> relation) const
    { return do_types<F>(relation); }

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

    /// The number of records of type T this layer holds itself (its parents' not counted).
    template<typename T>
        requires SupportsSymbol<Repository<SymbolRepo, RelationRepo>, T>
    [[nodiscard]] std::size_t own_size() const
    { return do_own_size<T>(); }

    /// Whether every record of type T that `source` can name is one this layer sees (urd::SeesAll): no layer of
    /// `source`'s chain below the first this layer sees holds one of its own (a sibling layer without its own, say).
    template<typename T>
        requires SupportsSymbol<Repository<SymbolRepo, RelationRepo>, T>
    [[nodiscard]] bool sees_all(const Repository& source) const
    {
        for (const Repository* layer = &source; layer != nullptr && !sees(*layer); layer = layer->m_parent)
        {
            if (layer->own_size<T>() != 0)
                return false;
        }
        return true;
    }

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
        requires HoldsRecords<Repository<SymbolRepo, RelationRepo>, T>
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

    /// A Schema's view, through the layer that owns it: a relation's pool of free relations resolves to it.
    [[nodiscard]] urd::index_view<Schema, Repository> view(urd::index<Schema> index) const;

    /// A Schema's record held outside the repository (a relation not interned yet) as its view here.
    [[nodiscard]] urd::borrowed_view<Schema, Repository> view(const urd::data<Schema>& schema URD_LIFETIMEBOUND) const { return { &schema, this }; }

    /// A row key as its view, through the layer that owns the row.
    template<typename F>
        requires HasRows<Repository<SymbolRepo, RelationRepo>, F>
    [[nodiscard]] RowView<F, Repository> view(urd::key<F, Row<F>> key) const
    { return do_view<F>(key); }

    /**
     * Relations of each family F with relations: records {pool, columns, row ids}, built by their users (rows interned
     * with insert(data<Row<F>>), columns with insert(data<Columns>)) and interned as any record.
     */

    /// The pool of free relations of these column types: their Schema, interned here.
    urd::index<Schema> schema(std::span<const TypeId> types)
        requires HasRows<Repository<SymbolRepo, RelationRepo>, Schema>
    { return do_schema(types); }

    /// The pool of free relations of these columns' types.
    urd::index<Schema> schema(std::span<const urd::data<ColumnLayout>> columns)
        requires HasRows<Repository<SymbolRepo, RelationRepo>, Schema>
    { return do_schema(columns); }

private:
    friend class KeyedRepository<Repository>;

    /// Its own parts that resolve keys (OwnParts): what KeyedRepository resolves a key through.
    [[nodiscard]] OwnParts own_parts() const;

    /// Its own parts that records are written into (OwnWritableParts): what KeyedRepository stores a record through.
    [[nodiscard]] OwnWritableParts own_writable_parts();

    // What a library instantiates: the constrained members above, unconstrained, which they forward to. Clang names an
    // instantiation's requires-clause in its symbol and g++ does not, so only unconstrained symbols link between code
    // built by either.
    template<typename T>
    [[nodiscard]] const Repository* do_owner(urd::index<T> index) const;
    template<typename F>
    [[nodiscard]] const Repository* do_owner(urd::key<F, Row<F>> key) const;
    template<typename F>
    [[nodiscard]] std::span<const TypeId> do_types(urd::index<F> relation) const;
    template<typename T>
    [[nodiscard]] bool do_contains(urd::index<T> index) const;
    template<typename T>
    [[nodiscard]] std::size_t do_size() const;
    template<typename T>
    [[nodiscard]] std::size_t do_own_size() const;
    template<typename T>
    [[nodiscard]] std::size_t do_memory_usage() const;
    template<typename T>
    [[nodiscard]] urd::index_view<T, Repository> do_view(urd::index<T> index) const;
    template<typename F>
    [[nodiscard]] std::size_t do_size(urd::index<F> relation) const;
    template<typename F>
    [[nodiscard]] std::size_t do_row_memory_usage() const;
    template<typename F>
    [[nodiscard]] RowView<F, Repository> do_view(urd::key<F, Row<F>> key) const;
    urd::index<Schema> do_schema(std::span<const TypeId> types);
    urd::index<Schema> do_schema(std::span<const urd::data<ColumnLayout>> columns);

    const Repository* m_parent;
    std::size_t m_layer;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace ygg::formalism

#endif
