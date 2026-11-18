/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_RELATION_DATA_HPP_
#define YGG_FORMALISM_RELATION_DATA_HPP_

#include "yggdrasil/formalism/columns_data.hpp"
#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/schema_data.hpp"
#include "yggdrasil/formalism/values.hpp"

#include <urd/elements/flat.hpp>
#include <utility>

namespace urd
{
/// A relation of family F: its pool (the key of the layer's pool of its rows), its columns and the keys of its rows, each
/// in that pool, sorted and unique. Renaming keeps the pool and the rows.
template<typename F>
struct data<ygg::Relation<F>>
{
    urd::index<ygg::Relation<F>> index;
    urd::index<F> pool;
    urd::index<ygg::Columns> columns;
    urd::flat_vector<ygg::RowIndex<F>> rows;

    data() = default;

    data(urd::index<F> pool_, urd::index<ygg::Columns> columns_, urd::flat_vector<ygg::RowIndex<F>> rows_) :
        index(),
        pool(pool_),
        columns(columns_),
        rows(std::move(rows_))
    {
    }

    data(const data& other) = delete;
    data& operator=(const data& other) = delete;
    data(data&& other) = default;
    data& operator=(data&& other) = default;
    ~data() = default;

    static constexpr auto fields() noexcept
    { return urd::Fields<urd::field<"pool", &data::pool>, urd::field<"columns", &data::columns>, urd::field<"rows", &data::rows>> {}; }

    static constexpr auto identifying_members() noexcept { return urd::members_of(fields()); }

    static constexpr auto cista_members() noexcept { return urd::Members<&data::index, &data::pool, &data::columns, &data::rows> {}; }
};
}  // namespace urd

#endif
