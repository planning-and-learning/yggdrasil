/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_DECLARATIONS_HPP_
#define YGG_DATABASE_DECLARATIONS_HPP_

#include "yggdrasil/core/byte_codec.hpp"
#include "yggdrasil/core/type_list.hpp"
#include "yggdrasil/core/types.hpp"

namespace ygg::database
{
/// Schema-local column identifiers, distinct from tuple values and positions.
struct Column
{
};
struct ColumnLayout;
struct Columns
{
};
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
template<typename Tag = void>
    requires(std::is_void_v<Tag> || InTypeList<Tag, QueryConstructorTags>)
struct Query
{
};
class QueryPlan;
class QueryRepository;
class QueryRepositoryFactory;
template<typename Tag = void>
using QueryView = ygg::View<ygg::Index<Query<Tag>>, QueryRepository>;

struct RelationRow
{
};
struct RelationRowSet
{
};
class Row;
class RelationPool;
class RelationPoolFactory;
class RelationRepository;
class RelationRepositoryFactory;
using RelationView = ygg::View<ygg::Index<Relation>, RelationRepository>;
using BorrowedRelationView = ygg::View<ygg::Builder<Relation>, RelationRepository>;
}  // namespace ygg::database

#endif
