/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_DECLARATIONS_HPP_
#define YGG_DATABASE_DECLARATIONS_HPP_

#include "yggdrasil/core/type_list.hpp"
#include "yggdrasil/core/types.hpp"
#include "yggdrasil/database/syntax/column_codec.hpp"

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
struct QueryInputTag
{
};
struct QueryEmptyTag
{
};
struct QueryJoinTag
{
};
struct QueryProjectTag
{
};
struct QueryRenameTag
{
};
struct QuerySelectEqualTag
{
};
struct QuerySelectValueTag
{
};
struct QueryUnionTag
{
};
struct QueryDifferenceTag
{
};
struct QueryDistanceTag
{
};
struct QueryGenericJoinTag
{
};
/// R ⋉ S: the rows of R with a match in S on their shared columns.
struct QuerySemijoinTag
{
};
/// R ▷ S: the rows of R without a match in S on their shared columns.
struct QueryAntijoinTag
{
};

using QueryConstructorTags = TypeList<QueryInputTag,
                                      QueryEmptyTag,
                                      QueryJoinTag,
                                      QueryProjectTag,
                                      QueryRenameTag,
                                      QuerySelectEqualTag,
                                      QuerySelectValueTag,
                                      QueryUnionTag,
                                      QueryDifferenceTag,
                                      QueryDistanceTag,
                                      QueryGenericJoinTag,
                                      QuerySemijoinTag,
                                      QueryAntijoinTag>;

/// Tag void is the type-erased query holding one of the concrete query records.
template<ColumnTypes Values = DefaultColumnTypes, typename Tag = void>
    requires(std::is_void_v<Tag> || InTypeList<Tag, QueryConstructorTags>)
struct Query
{
};
template<ColumnTypes Values = DefaultColumnTypes>
class QueryPlan;
template<ColumnTypes Values = DefaultColumnTypes>
class QueryRepository;
template<ColumnTypes Values = DefaultColumnTypes>
class QueryRepositoryFactory;
template<ColumnTypes Values = DefaultColumnTypes, typename Tag = void>
using QueryView = ygg::View<ygg::Index<Query<Values, Tag>>, QueryRepository<Values>>;

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
template<ColumnTypes Values = DefaultColumnTypes>
using BorrowedRelationView = ygg::View<ygg::Builder<Relation<Values>>, RelationRepository<Values>>;
}  // namespace ygg::database

#endif
