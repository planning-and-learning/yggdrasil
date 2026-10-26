/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_QUERY_QUERY_CONSTRUCTION_HPP_
#define YGG_DATABASE_QUERY_QUERY_CONSTRUCTION_HPP_

#include "yggdrasil/database/query/query_repository.hpp"
#include "yggdrasil/formalism/builder.hpp"
#include "yggdrasil/formalism/interning.hpp"

#include <algorithm>
#include <concepts>
#include <span>
#include <stdexcept>
#include <vector>

namespace ygg::database
{
using QueryBuilder = ApplyTypeListT<formalism::BuilderStorage, QuerySymbolTypes>;

using formalism::checkout;
using formalism::insert;

namespace detail
{
std::span<const ColumnLayout> child_columns(Index<Query<>> child, const QueryRepository& repository);

template<typename Tag>
    requires(std::same_as<Tag, QueryInputTag> || std::same_as<Tag, QueryEmptyTag>)
void prepare(Data<Query<Tag>>& data, const QueryRepository&)
{ validate_columns({ data.columns.data(), data.columns.size() }); }

template<typename Tag>
    requires(std::same_as<Tag, QueryJoinTag> || std::same_as<Tag, QuerySemijoinTag> || std::same_as<Tag, QueryAntijoinTag>)
void prepare(Data<Query<Tag>>& data, const QueryRepository& repository)
{ data.plan = JoinPlan(child_columns(data.lhs, repository), child_columns(data.rhs, repository)); }

template<typename Tag>
    requires(std::same_as<Tag, QueryUnionTag> || std::same_as<Tag, QueryDifferenceTag>)
void prepare(Data<Query<Tag>>& data, const QueryRepository& repository)
{
    const auto schema = child_columns(data.lhs, repository);
    require_plan_columns(child_columns(data.rhs, repository), schema);
    data.columns.set(schema.begin(), schema.end());
}

void prepare(Data<Query<QueryProjectTag>>& data, const QueryRepository& repository);

void prepare(Data<Query<QueryRenameTag>>& data, const QueryRepository& repository);

void prepare(Data<Query<QuerySelectEqualTag>>& data, const QueryRepository& repository);

void prepare(Data<Query<QuerySelectValueTag>>& data, const QueryRepository& repository);

void prepare(Data<Query<QueryDistanceTag>>& data, const QueryRepository& repository);

/// An omitted output order is canonicalized to first occurrence order.
void prepare(Data<Query<QueryGenericJoinTag>>& data, const QueryRepository& repository);
}  // namespace detail

/// Validates operands and derives the prepared metadata stored in the record.
template<typename Tag>
    requires(!std::same_as<Tag, void>)
void prepare_for_insert(QueryRepository& repository, Data<Query<Tag>>& data)
{ detail::prepare(data, repository); }

/// The concrete record must already be interned.
void prepare_for_insert(QueryRepository& repository, Data<Query<>>& data);

/// Interns a concrete query and then its heterogeneous root.
template<typename Tag>
QueryView<> insert_query(QueryRepository& repository, QueryBuilder& builder, Data<Query<Tag>>& data)
{
    auto root = checkout<Query<>>(builder);
    root->variant = insert(repository, data).first.get_index();
    return insert(repository, *root).first;
}
}  // namespace ygg::database

#endif
