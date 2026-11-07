/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_DECLARATIONS_HPP_
#define YGG_DATABASE_DECLARATIONS_HPP_

#include "yggdrasil/database/byte_codec.hpp"
#include <boost/mp11.hpp>
#include <urd/elements/flat.hpp>

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

using QueryConstructorTags = boost::mp11::mp_list<QueryInputTag,
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
    requires(std::is_void_v<Tag> || boost::mp11::mp_contains<QueryConstructorTags, Tag>::value)
struct Query
{
};
class QueryPlan;
class QueryRepository;
class QueryRepositoryFactory;
template<typename Tag = void>
using QueryView = urd::index_view<Query<Tag>, QueryRepository>;

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
using RelationView = urd::index_view<Relation, RelationRepository>;
using BorrowedRelationView = urd::borrowed_view<Relation, RelationRepository, urd::builder<Relation>>;
}  // namespace ygg::database

#endif
