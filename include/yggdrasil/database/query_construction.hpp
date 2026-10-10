/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_QUERY_CONSTRUCTION_HPP_
#define YGG_DATABASE_QUERY_CONSTRUCTION_HPP_

#include "yggdrasil/database/query_repository.hpp"
#include "yggdrasil/formalism/builder.hpp"
#include "yggdrasil/formalism/interning.hpp"

#include <algorithm>
#include <concepts>
#include <span>
#include <stdexcept>
#include <vector>

namespace ygg::database
{
template<ColumnTypes Values = DefaultColumnTypes>
using QueryBuilder = ApplyTypeListT<formalism::BuilderStorage, typename QueryRepository<Values>::SymbolTypes>;

using formalism::checkout;
using formalism::insert;

namespace detail
{
template<ColumnTypes Values>
std::span<const ColumnLayout> child_columns(Index<Query<Values>> child, const QueryRepository<Values>& repository)
{
    if (child.get_value() >= repository.size())
        throw std::invalid_argument("Query: children must precede their parent.");
    return QueryView<Values>(child, repository).columns();
}

template<ColumnTypes Values, typename Tag>
    requires(std::same_as<Tag, QueryInputTag> || std::same_as<Tag, QueryEmptyTag>)
void prepare(Data<Query<Values, Tag>>& data, const QueryRepository<Values>&)
{
    validate_columns<Values>({ data.columns.data(), data.columns.size() });
}

template<ColumnTypes Values>
void prepare(Data<Query<Values, QueryJoinTag>>& data, const QueryRepository<Values>& repository)
{
    data.plan = JoinPlan<Values>(child_columns(data.lhs, repository), child_columns(data.rhs, repository));
}

template<ColumnTypes Values, typename Tag>
    requires(std::same_as<Tag, QueryUnionTag> || std::same_as<Tag, QueryDifferenceTag>)
void prepare(Data<Query<Values, Tag>>& data, const QueryRepository<Values>& repository)
{
    const auto schema = child_columns(data.lhs, repository);
    require_plan_columns(child_columns(data.rhs, repository), schema);
    data.columns.set(schema.begin(), schema.end());
}

template<ColumnTypes Values>
void prepare(Data<Query<Values, QueryProjectTag>>& data, const QueryRepository<Values>& repository)
{
    data.plan = ProjectionPlan<Values>(child_columns(data.arg, repository), { data.labels.data(), data.labels.size() });
}

template<ColumnTypes Values>
void prepare(Data<Query<Values, QueryRenameTag>>& data, const QueryRepository<Values>& repository)
{
    Builder<Columns<Values>> schema(child_columns(data.arg, repository));
    schema.rename({ data.labels.data(), data.labels.size() });
    data.columns.set(schema.begin(), schema.end());
}

template<ColumnTypes Values>
void prepare(Data<Query<Values, QuerySelectEqualTag>>& data, const QueryRepository<Values>& repository)
{
    const auto schema = child_columns(data.arg, repository);
    data.lhs_position = column_index(schema, data.lhs_column);
    data.rhs_position = column_index(schema, data.rhs_column);
    if (schema[data.lhs_position].type != schema[data.rhs_position].type)
        throw std::invalid_argument("Query: equality columns must have the same type.");
    data.columns.set(schema.begin(), schema.end());
}

template<ColumnTypes Values>
void prepare(Data<Query<Values, QuerySelectValueTag>>& data, const QueryRepository<Values>& repository)
{
    const auto schema = child_columns(data.arg, repository);
    data.column_position = column_index(schema, data.column);
    auto field = schema[data.column_position];
    field.offset = 0;
    validate_row<Values>({ data.constant.data(), data.constant.size() }, std::span<const ColumnLayout>(&field, 1));
    data.columns.set(schema.begin(), schema.end());
}

template<ColumnTypes Values>
void prepare(Data<Query<Values, QueryDistanceTag>>& data, const QueryRepository<Values>& repository)
{
    data.plan = DistancePlan<Values>(child_columns(data.sources, repository),
                                     child_columns(data.edges, repository),
                                     child_columns(data.targets, repository),
                                     data.distance_column);
}

/// An omitted output order is canonicalized to first occurrence order.
template<ColumnTypes Values>
void prepare(Data<Query<Values, QueryGenericJoinTag>>& data, const QueryRepository<Values>& repository)
{
    std::vector<std::vector<ColumnLayout>> schemas;
    for (const auto child : data.inputs)
    {
        const auto schema = child_columns(child, repository);
        schemas.emplace_back(schema.begin(), schema.end());
    }
    if (data.output_order.empty())
        for (const auto& schema : schemas)
            for (const auto& column : schema)
                if (std::ranges::find(data.output_order, column.label) == data.output_order.end())
                    data.output_order.push_back(column.label);
    data.plan = GenericJoinPlan<Values>(schemas,
                                        { data.variable_order.data(), data.variable_order.size() },
                                        { data.output_order.data(), data.output_order.size() });
}
}  // namespace detail

/// Validates operands and derives the prepared metadata stored in the record.
template<ColumnTypes Values, typename Tag>
    requires(!std::same_as<Tag, void>)
void prepare_for_insert(QueryRepository<Values>& repository, Data<Query<Values, Tag>>& data)
{
    detail::prepare(data, repository);
}

/// The concrete record must already be interned.
template<ColumnTypes Values>
void prepare_for_insert(QueryRepository<Values>& repository, Data<Query<Values>>& data)
{
    data.variant.apply(
        [&]<typename Tag>(Index<Query<Values, Tag>> index)
        {
            if (index.get_value() >= repository.template size<Query<Values, Tag>>())
                throw std::invalid_argument("Query: root refers to a missing query.");
        });
}

/// Interns a concrete query and then its heterogeneous root.
template<ColumnTypes Values, typename Tag>
QueryView<Values> insert_query(QueryRepository<Values>& repository, QueryBuilder<Values>& builder, Data<Query<Values, Tag>>& data)
{
    auto root = checkout<Query<Values>>(builder);
    root->variant = insert(repository, data).first.get_index();
    return insert(repository, *root).first;
}
}  // namespace ygg::database

#endif
