#ifndef YGG_TESTS_UNIT_DATABASE_QUERY_HELPERS_HPP_
#define YGG_TESTS_UNIT_DATABASE_QUERY_HELPERS_HPP_

#include <array>
#include <initializer_list>
#include <span>
#include <stdexcept>
#include <yggdrasil/database/syntax/query.hpp>

/// Test-only query construction on top of checkout + insert_query.
namespace ygg::tests::qb
{
template<database::ColumnTypes Values = database::DefaultColumnTypes>
database::QueryRepository<Values> repository()
{
    static database::QueryRepositoryFactory<Values> factory;
    return factory.create();
}

template<database::ColumnTypes Values>
database::QueryBuilder<Values>& builder()
{
    static database::QueryBuilder<Values> storage;
    return storage;
}

template<database::ColumnTypes Values>
Index<database::Query<Values>> require(const database::QueryRepository<Values>& repository, database::QueryView<Values> query)
{
    if (&query.get_repository() != &repository)
        throw std::invalid_argument("Query: operand belongs to another repository.");
    return query.get_index();
}

template<database::ColumnTypes Values>
database::QueryView<Values> input(database::QueryRepository<Values>& repository, size_t slot, std::span<const database::ColumnLayout> columns)
{
    auto data = database::checkout<database::Query<Values, database::QueryInputTag>>(builder<Values>());
    data->input_slot = slot;
    data->columns.set(columns.begin(), columns.end());
    return database::insert_query(repository, builder<Values>(), *data);
}
template<database::ColumnTypes Values>
database::QueryView<Values> input(database::QueryRepository<Values>& repository, size_t slot, std::span<const Index<database::Column>> columns)
{
    const Builder<database::Columns<Values>> schema(columns);
    return input(repository, slot, schema.span());
}
template<database::ColumnTypes Values>
database::QueryView<Values> input(database::QueryRepository<Values>& repository, size_t slot, std::initializer_list<Index<database::Column>> columns)
{
    return input(repository, slot, std::span<const Index<database::Column>>(columns));
}

template<database::ColumnTypes Values>
database::QueryView<Values> empty(database::QueryRepository<Values>& repository, std::span<const database::ColumnLayout> columns)
{
    auto data = database::checkout<database::Query<Values, database::QueryEmptyTag>>(builder<Values>());
    data->columns.set(columns.begin(), columns.end());
    return database::insert_query(repository, builder<Values>(), *data);
}
template<database::ColumnTypes Values>
database::QueryView<Values> empty(database::QueryRepository<Values>& repository, std::span<const Index<database::Column>> columns)
{
    const Builder<database::Columns<Values>> schema(columns);
    return empty(repository, schema.span());
}
template<database::ColumnTypes Values>
database::QueryView<Values> empty(database::QueryRepository<Values>& repository, std::initializer_list<Index<database::Column>> columns)
{
    return empty(repository, std::span<const Index<database::Column>>(columns));
}

template<typename Tag, database::ColumnTypes Values>
database::QueryView<Values> binary(database::QueryRepository<Values>& repository, database::QueryView<Values> lhs, database::QueryView<Values> rhs)
{
    auto data = database::checkout<database::Query<Values, Tag>>(builder<Values>());
    data->lhs = require(repository, lhs);
    data->rhs = require(repository, rhs);
    return database::insert_query(repository, builder<Values>(), *data);
}
template<database::ColumnTypes Values>
database::QueryView<Values> join(database::QueryRepository<Values>& repository, database::QueryView<Values> lhs, database::QueryView<Values> rhs)
{
    return binary<database::QueryJoinTag>(repository, lhs, rhs);
}
template<database::ColumnTypes Values>
database::QueryView<Values> union_(database::QueryRepository<Values>& repository, database::QueryView<Values> lhs, database::QueryView<Values> rhs)
{
    return binary<database::QueryUnionTag>(repository, lhs, rhs);
}
template<database::ColumnTypes Values>
database::QueryView<Values> difference(database::QueryRepository<Values>& repository, database::QueryView<Values> lhs, database::QueryView<Values> rhs)
{
    return binary<database::QueryDifferenceTag>(repository, lhs, rhs);
}

template<database::ColumnTypes Values>
database::QueryView<Values> semijoin(database::QueryRepository<Values>& repository, database::QueryView<Values> lhs, database::QueryView<Values> rhs)
{
    return binary<database::QuerySemijoinTag>(repository, lhs, rhs);
}
template<database::ColumnTypes Values>
database::QueryView<Values> antijoin(database::QueryRepository<Values>& repository, database::QueryView<Values> lhs, database::QueryView<Values> rhs)
{
    return binary<database::QueryAntijoinTag>(repository, lhs, rhs);
}

template<typename Tag, database::ColumnTypes Values>
database::QueryView<Values>
relabel(database::QueryRepository<Values>& repository, database::QueryView<Values> arg, std::span<const Index<database::Column>> labels)
{
    auto data = database::checkout<database::Query<Values, Tag>>(builder<Values>());
    data->arg = require(repository, arg);
    data->labels.set(labels.begin(), labels.end());
    return database::insert_query(repository, builder<Values>(), *data);
}
template<database::ColumnTypes Values>
database::QueryView<Values>
project(database::QueryRepository<Values>& repository, database::QueryView<Values> arg, std::span<const Index<database::Column>> labels)
{
    return relabel<database::QueryProjectTag>(repository, arg, labels);
}
template<database::ColumnTypes Values>
database::QueryView<Values>
project(database::QueryRepository<Values>& repository, database::QueryView<Values> arg, std::initializer_list<Index<database::Column>> labels)
{
    return project(repository, arg, std::span<const Index<database::Column>>(labels));
}
template<database::ColumnTypes Values>
database::QueryView<Values>
rename(database::QueryRepository<Values>& repository, database::QueryView<Values> arg, std::span<const Index<database::Column>> labels)
{
    return relabel<database::QueryRenameTag>(repository, arg, labels);
}
template<database::ColumnTypes Values>
database::QueryView<Values>
rename(database::QueryRepository<Values>& repository, database::QueryView<Values> arg, std::initializer_list<Index<database::Column>> labels)
{
    return rename(repository, arg, std::span<const Index<database::Column>>(labels));
}

template<database::ColumnTypes Values>
database::QueryView<Values>
select_equal(database::QueryRepository<Values>& repository, database::QueryView<Values> arg, Index<database::Column> lhs, Index<database::Column> rhs)
{
    auto data = database::checkout<database::Query<Values, database::QuerySelectEqualTag>>(builder<Values>());
    data->arg = require(repository, arg);
    data->lhs_column = lhs;
    data->rhs_column = rhs;
    return database::insert_query(repository, builder<Values>(), *data);
}
template<database::ColumnTypes Values>
database::QueryView<Values>
select_encoded(database::QueryRepository<Values>& repository, database::QueryView<Values> arg, Index<database::Column> column, std::span<const std::byte> value)
{
    auto data = database::checkout<database::Query<Values, database::QuerySelectValueTag>>(builder<Values>());
    data->arg = require(repository, arg);
    data->column = column;
    data->constant.set(value.begin(), value.end());
    data->constant_type = arg.columns()[database::column_index(arg.columns(), column)].type;
    return database::insert_query(repository, builder<Values>(), *data);
}
template<database::ColumnTypes Values, database::ColumnValueFor<Values> T>
database::QueryView<Values>
select_value(database::QueryRepository<Values>& repository, database::QueryView<Values> arg, Index<database::Column> column, T value)
{
    if (arg.columns()[database::column_index(arg.columns(), column)].type != database::column_type<Values, T>)
        throw std::invalid_argument("Query: constant type does not match selected column.");
    std::array<std::byte, database::ColumnCodec<T>::size> bytes;
    database::ColumnCodec<T>::encode(value, bytes);
    return select_encoded(repository, arg, column, bytes);
}

template<database::ColumnTypes Values>
database::QueryView<Values> distance(database::QueryRepository<Values>& repository,
                                     database::QueryView<Values> sources,
                                     database::QueryView<Values> edges,
                                     database::QueryView<Values> targets,
                                     Index<database::Column> distance_column)
{
    auto data = database::checkout<database::Query<Values, database::QueryDistanceTag>>(builder<Values>());
    data->sources = require(repository, sources);
    data->edges = require(repository, edges);
    data->targets = require(repository, targets);
    data->distance_column = distance_column;
    return database::insert_query(repository, builder<Values>(), *data);
}

template<database::ColumnTypes Values>
database::QueryView<Values> generic_join(database::QueryRepository<Values>& repository,
                                         std::span<const database::QueryView<Values>> inputs,
                                         std::span<const Index<database::Column>> variable_order,
                                         std::span<const Index<database::Column>> output_order = {})
{
    auto data = database::checkout<database::Query<Values, database::QueryGenericJoinTag>>(builder<Values>());
    for (const auto input : inputs)
        data->inputs.push_back(require(repository, input));
    data->variable_order.set(variable_order.begin(), variable_order.end());
    data->output_order.set(output_order.begin(), output_order.end());
    return database::insert_query(repository, builder<Values>(), *data);
}
template<database::ColumnTypes Values>
database::QueryView<Values> generic_join(database::QueryRepository<Values>& repository,
                                         std::initializer_list<database::QueryView<Values>> inputs,
                                         std::initializer_list<Index<database::Column>> variable_order,
                                         std::initializer_list<Index<database::Column>> output_order = {})
{
    return generic_join(repository,
                        std::span<const database::QueryView<Values>>(inputs),
                        std::span<const Index<database::Column>>(variable_order),
                        std::span<const Index<database::Column>>(output_order));
}
}  // namespace ygg::tests::qb

#endif
