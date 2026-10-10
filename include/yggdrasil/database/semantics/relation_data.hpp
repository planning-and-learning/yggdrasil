/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_RELATION_DATA_HPP_
#define YGG_DATABASE_SEMANTICS_RELATION_DATA_HPP_

#include "yggdrasil/core/types_utils.hpp"
#include "yggdrasil/database/semantics/relation_index.hpp"

#include <cstddef>
#include <tuple>

namespace ygg
{
template<database::ColumnTypes Values>
struct Data<database::Relation<Values>>
{
    Index<database::Relation<Values>> index;
    Index<database::Columns<Values>> columns_index;
    Index<database::RelationRowSet<Values>> row_set_index;
    size_t schema_namespace = 0;

    void clear() noexcept
    {
        ygg::clear(index);
        ygg::clear(columns_index);
        ygg::clear(row_set_index);
        schema_namespace = 0;
    }

    auto cista_members() const noexcept { return std::tie(index, columns_index, row_set_index, schema_namespace); }
    auto identifying_members() const noexcept { return std::tie(columns_index, row_set_index, schema_namespace); }
};
}  // namespace ygg

#endif
