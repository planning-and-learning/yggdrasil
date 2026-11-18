/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_RELATION_BUILDER_HPP_
#define YGG_FORMALISM_RELATION_BUILDER_HPP_

#include "yggdrasil/core/config.hpp"
#include "yggdrasil/formalism/columns_builder.hpp"
#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/interning.hpp"
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
#include <utility>
#include <vector>

namespace urd
{
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
    urd::child<F, C> pool;
    urd::child<ygg::Columns, C> columns;
    std::vector<urd::child<ygg::Row<F>, C, ygg::RowIndex<F>>> rows;

    static constexpr auto fields() noexcept
    { return urd::Fields<urd::field<"pool", &builder::pool>, urd::field<"columns", &builder::columns>, urd::field<"rows", &builder::rows>> {}; }

    static constexpr auto identifying_members() noexcept { return urd::members_of(fields()); }

    /// Empties it and gives it the pool `pool` and these columns (validated), interned into `repository`.
    void initialize(C& repository, urd::index<F> pool_, std::span<const ygg::ColumnLayout> layouts)
    {
        columns = interned_columns(repository, layouts);
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
        columns = interned_columns(repository, layouts);
    }

    /// Inserts a row of values, one per column, at its place in tuple order, where no row of it has these values:
    /// interned into `repository` in its pool (a count other than the columns' throws std::invalid_argument, a value
    /// that does not fit its column std::out_of_range, before anything is stored). Its position, and whether it was
    /// inserted.
    std::pair<std::size_t, bool> insert(C& repository, std::span<const ygg::Value> values)
    {
        const auto key = pool_key(repository);
        const auto [at, held] = repository.view(*this).locate(values);
        if (held)
            return { at, false };
        rows.emplace(rows.begin() + static_cast<std::ptrdiff_t>(at), repository.view(ygg::RowIndex<F> { key, repository.insert_row(key, values).first }));
        return { at, true };
    }

    /// Erases the row of these values; whether it held one. Its row stays in the repository.
    bool erase(const C& repository, std::span<const ygg::Value> values)
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
    /// These columns (validated), interned into `repository`.
    static urd::child<ygg::Columns, C> interned_columns(C& repository, std::span<const ygg::ColumnLayout> layouts)
    {
        ygg::validate_columns(layouts);
        auto record = urd::data<ygg::Columns>(urd::flat_vector<ygg::ColumnLayout>(layouts.begin(), layouts.end()));
        return repository.insert(record).first;
    }

    /// The key of its pool, interned into `repository` where it is built or not interned yet: its rows' keys name it.
    urd::index<F> pool_key(C& repository)
    {
        if (pool.index() == 1)
            pool = repository.view(urd::inserted_index(ygg::insert_child(repository, std::as_const(std::get<1>(pool).held()))));
        else if (pool.index() == 2)
            pool = repository.view(urd::inserted_index(ygg::insert_child(repository, std::get<2>(pool).held())));
        return std::get<0>(pool).index();
    }
};

}  // namespace urd

#endif
