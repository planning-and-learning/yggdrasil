/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_RELATION_DATA_HPP_
#define YGG_DATABASE_SEMANTICS_RELATION_DATA_HPP_

#include "yggdrasil/core/types_utils.hpp"
#include "yggdrasil/database/declarations.hpp"

#include <cstddef>
#include <tuple>
#include <utility>

namespace ygg
{
template<>
struct Data<database::Relation>
{
    Index<database::Relation> index;
    Index<database::Columns> columns_index;
    Index<database::RelationRowSet> row_set_index;
    size_t schema_namespace = 0;

    Data() = default;
    Data(Index<database::Columns> columns_index_, Index<database::RelationRowSet> row_set_index_, size_t schema_namespace_) :
        index(),
        columns_index(columns_index_),
        row_set_index(row_set_index_),
        schema_namespace(schema_namespace_)
    {
    }
    template<typename C>
    Data(::ygg::View<Index<database::Columns>, C> columns_, Index<database::RelationRowSet> row_set_index_, size_t schema_namespace_) :
        index(),
        columns_index(),
        row_set_index(row_set_index_),
        schema_namespace(schema_namespace_)
    {
        set(columns_, columns_index);
    }

    auto cista_members() noexcept { return std::tie(index, columns_index, row_set_index, schema_namespace); }
    auto cista_members() const noexcept { return std::tie(index, columns_index, row_set_index, schema_namespace); }
    static constexpr auto identifying_members() noexcept { return Members<&Data::columns_index, &Data::row_set_index, &Data::schema_namespace> {}; }
    void clear() noexcept
    {
        std::apply([](auto&... member) { (ygg::clear(member), ...); }, cista_members());
    }
};
}  // namespace ygg

#endif
