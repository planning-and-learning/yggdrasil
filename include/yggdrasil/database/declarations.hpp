/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_DECLARATIONS_HPP_
#define YGG_DATABASE_DECLARATIONS_HPP_

#include "yggdrasil/core/types.hpp"
#include "yggdrasil/database/column_codec.hpp"

namespace ygg::database
{
/// Schema-local column identifiers, distinct from tuple values and positions.
struct Column
{
};
struct ColumnLayout;
template<ColumnTypes Values = DefaultColumnTypes>
struct Columns
{
};
template<ColumnTypes Values = DefaultColumnTypes>
struct Relation
{
};
template<ColumnTypes Values = DefaultColumnTypes>
struct RelationRow
{
};
template<ColumnTypes Values = DefaultColumnTypes>
struct RelationRowSet
{
};
template<ColumnTypes Values = DefaultColumnTypes>
class Row;
template<ColumnTypes Values = DefaultColumnTypes>
class RelationPool;
template<ColumnTypes Values = DefaultColumnTypes>
class RelationPoolFactory;
template<ColumnTypes Values = DefaultColumnTypes>
class RelationRepository;
template<ColumnTypes Values = DefaultColumnTypes>
class RelationRepositoryFactory;
template<ColumnTypes Values = DefaultColumnTypes>
using RelationView = ygg::View<ygg::Index<Relation<Values>>, RelationRepository<Values>>;
}  // namespace ygg::database

#endif
