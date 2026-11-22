/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_SCRATCH_RELATION_HPP_
#define YGG_FORMALISM_SCRATCH_RELATION_HPP_

#include "yggdrasil/formalism/column_layout.hpp"
#include "yggdrasil/formalism/columns_data.hpp"
#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/free_relations.hpp"
#include "yggdrasil/formalism/relation_builder.hpp"
#include "yggdrasil/formalism/schema_data.hpp"

#include <concepts>
#include <cstdint>
#include <ranges>
#include <span>
#include <urd/elements/handle.hpp>
#include <urd/repr/containers/schema_rows.hpp>
#include <urd/repr/slots/slots.hpp>
#include <urd/store.hpp>
#include <variant>
#include <vector>

namespace ygg
{
/// A relation being built together with the storage of its rows: its own repository C (no parent: its pool and columns
/// are interned there), the builders of its rows (a slots Store: a row is a builder, its relation and its values, held by
/// its handle, its slot reused once the row is erased, so its storage is that of the rows it holds) and its builder,
/// read through its repository (view()). Its rows are builders by its own invariant: insert() puts them there. It does not move: its builder's
/// children are views into its repository and handles into its records.
template<typename F, typename C>
struct ScratchRelation
{
    C rows;
    using Records = RowRecords<F, C>;
    Records records;
    /// A row's slot in its records (slot()).
    using Slot = typename Records::handle_type;
    urd::builder<Relation<F>, C> relation;

    /// An empty one (a repository's default constructor is explicit: so is not an aggregate's).
    ScratchRelation() : rows(), records(), relation(), columns_kept(), schema_kept() {}

    /// An empty one for relations of `arity` columns, as ScratchRelations keeps them (its records' buffers fit them).
    explicit ScratchRelation(std::size_t /*arity*/) : ScratchRelation() {}

    /// Inserts a row of values at its place in tuple order, as a builder of its records (builder's insert).
    template<RowValues Values>
    std::pair<std::size_t, bool> insert(const Values& values)
    { return relation.insert(rows, records, values); }

    /// Erases the row of these values; whether it held one. Its builder goes back to its records.
    template<RowValues Values>
    bool erase(const Values& values)
    { return relation.erase(rows, values); }

    /// Gives its builder these columns (validated) and, as its pool, the Schema of their types, inserted into its
    /// repository as any record (written in schema_kept: no allocation once warm).
    void initialize(std::span<const urd::data<ColumnLayout>> columns)
        requires std::same_as<F, Schema>
    {
        const auto types = columns | std::views::transform(&urd::data<ColumnLayout>::type);
        schema_kept.column_types.set(types.begin(), types.end());
        relation.initialize(rows, rows.insert(schema_kept).first.index(), columns);
    }

    /// Its builder, read through its repository.
    [[nodiscard]] RelationBuilderView<F, C> view() const { return rows.view(relation); }

    /// Empties it: its rows' records go back to its records, its repository is cleared (it keeps its pools' buffers and
    /// its pool keys, so a refill allocates nothing); its columns and pool stay.
    void clear()
    {
        const auto columns = view().columns();
        columns_kept.values.set(columns.begin(), columns.end());               // its buffer kept: no allocation once warm
        const auto pool = std::get<urd::interned_child<F, C>>(relation.pool);  // interned by its initialization
        rows.clear();
        relation.columns = rows.insert(columns_kept).first.index();  // validated when it was initialized
        relation.pool = pool;
        relation.clear();
    }

    /// The slot of the builder of the row of these values in its records, where it holds one (else none): a row's identity
    /// that stays while it holds the row, unlike its position, and a dense number, unlike its record's address.
    template<RowValues Values>
    [[nodiscard]] Slot slot(const Values& values) const
    {
        const auto found = relation.rows.find(values);
        return found == relation.rows.end() ? Slot() :
                                              Slot(static_cast<typename Slot::value_type>(
                                                  std::get<urd::built_child<Row<F>, C>>(*found).get_deleter().slot));  // insert() holds its rows as records
    }

    /// The row builder in a slot of its records (a row's, where it holds that row).
    [[nodiscard]] const urd::builder<Row<F>, C>& at(Slot slot) const { return records[slot]; }

    /// Its columns across a clear(), which forgets their record: clear()'s, kept so that a refill allocates nothing.
    urd::data<Columns> columns_kept;
    /// The Schema of its free relation's column types, written by initialize(columns): kept so that it allocates nothing.
    urd::data<Schema> schema_kept;
};

/// The scratch relations a computation draws (draw()): each held by its handle and given back with it, its
/// repository's and its records' buffers kept for the next of the same arity (its slots are keyed by arity, so that a
/// relation is drawn with row records already of its width).
template<typename F, typename C>
class ScratchRelations
{
public:
    /// A scratch relation of these columns (validated) in the pool `pool`: its repository cleared, its builder
    /// initialized.
    [[nodiscard]] urd::handle<ScratchRelation<F, C>> draw(urd::index<F> pool, std::span<const urd::data<ColumnLayout>> columns)
    {
        auto drawn = m_slots.make_handle(columns.size());
        drawn->rows.clear();
        drawn->relation.initialize(drawn->rows, pool, columns);
        return drawn;
    }

    /// A free relation of these columns (validated), its pool the Schema of their types.
    [[nodiscard]] urd::handle<ScratchRelation<F, C>> draw(std::span<const urd::data<ColumnLayout>> columns)
        requires std::same_as<F, Schema>
    {
        auto drawn = m_slots.make_handle(columns.size());
        drawn->rows.clear();
        drawn->initialize(columns);
        return drawn;
    }

private:
    urd::Store<urd::slots<ScratchRelation<F, C>>, urd::keyed<std::size_t>> m_slots;
};

/// A free relation being built with its rows' storage, and the scratch relations to draw them from.
using FreeScratchRelation = ScratchRelation<Schema, FreeRelationRepository>;
using FreeScratchRelations = ScratchRelations<Schema, FreeRelationRepository>;
}  // namespace ygg

#endif
