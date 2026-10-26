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
/// Per row of a groups input, an aggregate (COUNT, SUM, MIN, MAX) of the argument's rows that agree with it.
struct QueryAggregateTag
{
};
/// Inside the bodies of a fixpoint (its scope), relation `variable` of that fixpoint as of the previous iteration: the
/// feedback edge z⁻¹ of DBSP's fixpoint circuit [Budiu23, §5].
struct QueryRecursionTag
{
};
/// Relation `variable` of the least fixpoint of its bodies: relation v is body v, whose recursion leaves of the same
/// scope read the relations of the previous iteration, starting empty.
struct QueryFixpointTag
{
};

enum class AggregateFunction : uint8_t
{
    /// The number of rows; 0 for a group without rows.
    COUNT,
    /// The sum of a column; 0 for a group without rows.
    SUM,
    /// The least value of a column; a group without rows has none.
    MIN,
    /// The greatest value of a column; a group without rows has none.
    MAX,
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
                                      QueryAntijoinTag,
                                      QueryAggregateTag,
                                      QueryRecursionTag,
                                      QueryFixpointTag>;

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
class RelationPool;
class RelationPoolFactory;
class RelationRepository;
class RelationRepositoryFactory;
using RelationView = ygg::View<ygg::Index<Relation>, RelationRepository>;
using BorrowedRelationView = ygg::View<ygg::Builder<Relation>, RelationRepository>;
}  // namespace ygg::database

#endif
