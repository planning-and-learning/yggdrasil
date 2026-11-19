/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_RELATION_BUILDER_HPP_
#define YGG_FORMALISM_RELATION_BUILDER_HPP_

#include "yggdrasil/core/config.hpp"
#include "yggdrasil/formalism/columns_builder.hpp"
#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/relation_data.hpp"
#include "yggdrasil/formalism/relation_view.hpp"
#include "yggdrasil/formalism/row_builder.hpp"
#include "yggdrasil/formalism/row_view.hpp"
#include "yggdrasil/formalism/schema_builder.hpp"
#include "yggdrasil/formalism/values.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <stdexcept>
#include <urd/elements/builder.hpp>
#include <urd/elements/children.hpp>
#include <urd/elements/flat.hpp>
#include <urd/elements/view.hpp>
#include <urd/repr/values/sorted_block_set.hpp>
#include <urd/store.hpp>
#include <utility>
#include <variant>
#include <vector>

namespace urd
{
/// A relation's rows as its builder holds them: row children (an interned row, a row builder, or a row record not
/// interned yet), in tuple order and unique, as its record keeps their keys.
template<typename F, typename C>
struct built_form<urd::flat_vector<ygg::RowIndex<F>>, C>
{
    using type = urd::Store<urd::multiset<urd::child<ygg::Row<F>, C, ygg::RowIndex<F>>>, urd::sorted, urd::unique, urd::order_by<ygg::TupleOrder>>;
};

/// The builder of a relation of family F: its record's members, each index a child (a view through its own context).
/// Its rows are row children (an interned row, a row builder, or a row record not interned yet), in tuple order and
/// unique, as its record's: its members below keep them so, interning rows and columns into the repository they are
/// given. It is read as any builder, through a view (urd::resolve(builder, repository), repository.view(builder): the
/// relation mixin of relation_view.hpp), and interned with ygg::intern. A zero-column relation is true exactly when it
/// contains the empty row.
template<typename F, typename C>
struct builder<ygg::Relation<F>, C>
{
    urd::index<ygg::Relation<F>> index;
    urd::built_t<urd::index<F>, C> pool;
    urd::built_t<urd::index<ygg::Columns>, C> columns;
    urd::built_t<urd::flat_vector<ygg::RowIndex<F>>, C> rows;

    static constexpr auto fields() noexcept
    { return urd::Fields<urd::field<"pool", &builder::pool>, urd::field<"columns", &builder::columns>, urd::field<"rows", &builder::rows>> {}; }

    static constexpr auto identifying_members() noexcept { return urd::members_of(fields()); }

    /// Empties it and gives it the pool `pool` and these columns (validated), interned into `repository`.
    void initialize(C& repository, urd::index<F> pool_, std::span<const ygg::ColumnLayout> layouts)
    {
        assign_columns(repository, layouts);
        pool = repository.view(pool_);
        rows.clear();
    }

    /// Empties a free relation's builder and gives it these columns (validated), its pool the Schema of their types; both
    /// interned into `repository`.
    void initialize(C& repository, std::span<const ygg::ColumnLayout> layouts)
        requires std::same_as<F, ygg::Schema>
    { initialize(repository, repository.schema(layouts), layouts); }

    /// Relabels its columns (validated), the same types: its pool and its rows stay.
    void rename(C& repository, std::span<const ygg::ColumnLayout> layouts)
    {
        if (!std::ranges::equal(repository.view(*this).columns(), layouts, {}, &ygg::ColumnLayout::type, &ygg::ColumnLayout::type))
            throw std::invalid_argument("Columns: relabeling must keep the arity and column types.");
        assign_columns(repository, layouts);
    }

    /// Inserts a row of values (as they are held: contiguous, or a stored row's), one per column, at its place in tuple
    /// order (O(log n): its rows are a sorted set in blocks), where no row of it has these values: a row record drawn
    /// from `records` (a Store of slots of data<Row<F>>), held by its handle and read through `repository`; erasing the
    /// row gives the record back to `records`, buffers and all. A count other than the columns' throws
    /// std::invalid_argument, a value wider than its column's type std::out_of_range, before anything is stored. Its
    /// position, and whether it was inserted.
    template<ygg::RowValues Values, typename Records>
    std::pair<std::size_t, bool> insert(C& repository, Records& records, const Values& values)
    {
        // Its rows' records name its pool by its key: a pool built or not interned yet is interned first, with the
        // relation (repository.insert(builder, storage)), not row by row (std::bad_variant_access).
        const auto key = urd::interned_view(pool).index();
        const auto view = repository.view(*this);
        if (static_cast<std::size_t>(std::ranges::distance(values)) != view.arity())
            throw std::invalid_argument("Relation: a row has one value per column.");
        auto column = view.columns().begin();
        for (const ygg::Value value : values)
        {
            // A column's values are of its type's width: a record is no packed row, so it is checked here.
            if (const auto bits = ygg::column_bits((column++)->type, ygg::whole_bits<urd::index<ygg::formalism::Object>>); bits < 64 && (value >> bits) != 0)
                throw std::out_of_range("Relation: a value does not fit its column.");
        }
        const auto [at, held] = view.locate(values);
        if (held)
            return { at, false };
        auto record = records.make_handle();
        record->relation = key;
        record->values.set(std::ranges::begin(values), std::ranges::end(values));
        rows.insert(urd::pooled_view<ygg::Row<F>, C, urd::data<ygg::Row<F>>>(std::move(record), &repository));
        return { at, true };
    }

    /// Erases the row of these values; whether it held one. A row record goes back to its pool.
    template<ygg::RowValues Values>
    bool erase(const C& repository, const Values& values)
    {
        const auto [at, held] = repository.view(*this).locate(values);
        if (!held)
            return false;
        rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(at));
        return true;
    }

    /// Erases its rows; their rows stay in the repository.
    void clear() noexcept { rows.clear(); }

private:
    /// Gives it these columns (validated), interned into `repository`.
    void assign_columns(C& repository, std::span<const ygg::ColumnLayout> layouts)
    {
        ygg::validate_columns(layouts);
        auto record = urd::data<ygg::Columns>(urd::flat_vector<ygg::ColumnLayout>(layouts.begin(), layouts.end()));
        columns = repository.insert(record).first;
    }
};

}  // namespace urd

#endif
