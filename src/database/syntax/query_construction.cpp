/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/syntax/query_construction.hpp"

namespace ygg::database::detail
{
std::span<const ColumnLayout> child_columns(Index<Query<>> child, const QueryRepository& repository)
{
    if (child.get_value() >= repository.size())
        throw std::invalid_argument("Query: children must precede their parent.");
    return QueryView<>(child, repository).columns();
}
}

namespace ygg::database::detail
{
void prepare(Data<Query<QueryProjectTag>>& data, const QueryRepository& repository)
{ data.plan = ProjectionPlan(child_columns(data.arg, repository), { data.labels.data(), data.labels.size() }); }
}

namespace ygg::database::detail
{
void prepare(Data<Query<QueryRenameTag>>& data, const QueryRepository& repository)
{
    Builder<Columns> schema(child_columns(data.arg, repository));
    schema.rename({ data.labels.data(), data.labels.size() });
    data.columns.set(schema.begin(), schema.end());
}
}

namespace ygg::database::detail
{
void prepare(Data<Query<QuerySelectEqualTag>>& data, const QueryRepository& repository)
{
    const auto schema = child_columns(data.arg, repository);
    data.lhs_position = column_index(schema, data.lhs_column);
    data.rhs_position = column_index(schema, data.rhs_column);
    if (schema[data.lhs_position].type != schema[data.rhs_position].type)
        throw std::invalid_argument("Query: equality columns must have the same type.");
    data.columns.set(schema.begin(), schema.end());
}
}

namespace ygg::database::detail
{
void prepare(Data<Query<QuerySelectValueTag>>& data, const QueryRepository& repository)
{
    const auto schema = child_columns(data.arg, repository);
    data.column_position = column_index(schema, data.column);
    if (schema[data.column_position].type != data.constant_type)
        throw std::invalid_argument("Query: constant type does not match the selected column.");
    if (data.constant.size() != schema[data.column_position].size)
        throw std::invalid_argument("Query: constant width does not match the selected column.");
    data.columns.set(schema.begin(), schema.end());
}
}

namespace ygg::database::detail
{
void prepare(Data<Query<QueryDistanceTag>>& data, const QueryRepository& repository)
{
    data.plan = DistancePlan(child_columns(data.sources, repository),
                             child_columns(data.edges, repository),
                             child_columns(data.targets, repository),
                             data.distance_column);
}
}

namespace ygg::database::detail
{
void prepare(Data<Query<QueryGenericJoinTag>>& data, const QueryRepository& repository)
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
    data.plan = GenericJoinPlan(schemas, { data.variable_order.data(), data.variable_order.size() }, { data.output_order.data(), data.output_order.size() });
}
}

namespace ygg::database
{
void prepare_for_insert(QueryRepository& repository, Data<Query<>>& data)
{
    data.variant.apply(
        [&]<typename Tag>(Index<Query<Tag>> index)
        {
            if (index.get_value() >= repository.template size<Query<Tag>>())
                throw std::invalid_argument("Query: root refers to a missing query.");
        });
}
}
