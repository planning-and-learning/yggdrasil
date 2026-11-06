/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_RELATION_DATA_HPP_
#define YGG_DATABASE_SEMANTICS_RELATION_DATA_HPP_

#include <urd/elements/flat.hpp>
#include "yggdrasil/database/declarations.hpp"

#include <cstddef>
#include <tuple>
#include <utility>

namespace urd
{
template<>
struct data<ygg::database::Relation>
{
    urd::index<ygg::database::Relation> index;
    urd::index<ygg::database::Columns> columns_index;
    urd::index<ygg::database::RelationRowSet> row_set_index;
    size_t schema_namespace = 0;

    data() = default;
    data(urd::index<ygg::database::Columns> columns_index_, urd::index<ygg::database::RelationRowSet> row_set_index_, size_t schema_namespace_) :
        index(),
        columns_index(columns_index_),
        row_set_index(row_set_index_),
        schema_namespace(schema_namespace_)
    {
    }
    template<typename C>
    data(urd::view<ygg::database::Columns, C> columns_, urd::index<ygg::database::RelationRowSet> row_set_index_, size_t schema_namespace_) :
        index(),
        columns_index(urd::to_stored(columns_)),
        row_set_index(row_set_index_),
        schema_namespace(schema_namespace_)
    {
    }

    static constexpr auto cista_members() noexcept
    { return urd::Members<&data::index, &data::columns_index, &data::row_set_index, &data::schema_namespace> {}; }
    static constexpr auto identifying_members() noexcept { return urd::Members<&data::columns_index, &data::row_set_index, &data::schema_namespace> {}; }
};
}  // namespace urd

#endif
