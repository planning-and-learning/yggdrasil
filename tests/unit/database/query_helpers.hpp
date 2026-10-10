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
namespace db = ygg::database;

template<db::ColumnTypes Values = db::DefaultColumnTypes>
db::QueryRepository<Values> repository()
{
    static db::QueryRepositoryFactory<Values> factory;
    return factory.create();
}

template<db::ColumnTypes Values>
db::QueryBuilder<Values>& builder()
{
    static db::QueryBuilder<Values> storage;
    return storage;
}

template<db::ColumnTypes Values>
Index<db::Query<Values>> require(const db::QueryRepository<Values>& repository, db::QueryView<Values> query)
{
    if (&query.get_repository() != &repository)
        throw std::invalid_argument("Query: operand belongs to another repository.");
    return query.get_index();
}

template<db::ColumnTypes Values>
db::QueryView<Values> input(db::QueryRepository<Values>& repository, size_t slot, std::span<const db::ColumnLayout> columns)
{
    auto data = db::checkout<db::Query<Values, db::QueryInputTag>>(builder<Values>());
    data->input_slot = slot;
    data->columns.set(columns.begin(), columns.end());
    return db::insert_query(repository, builder<Values>(), *data);
}
template<db::ColumnTypes Values>
db::QueryView<Values> input(db::QueryRepository<Values>& repository, size_t slot, std::span<const Index<db::Column>> columns)
{
    const Builder<db::Columns<Values>> schema(columns);
    return input(repository, slot, schema.span());
}
template<db::ColumnTypes Values>
db::QueryView<Values> input(db::QueryRepository<Values>& repository, size_t slot, std::initializer_list<Index<db::Column>> columns)
{
    return input(repository, slot, std::span<const Index<db::Column>>(columns));
}

template<db::ColumnTypes Values>
db::QueryView<Values> empty(db::QueryRepository<Values>& repository, std::span<const db::ColumnLayout> columns)
{
    auto data = db::checkout<db::Query<Values, db::QueryEmptyTag>>(builder<Values>());
    data->columns.set(columns.begin(), columns.end());
    return db::insert_query(repository, builder<Values>(), *data);
}
template<db::ColumnTypes Values>
db::QueryView<Values> empty(db::QueryRepository<Values>& repository, std::span<const Index<db::Column>> columns)
{
    const Builder<db::Columns<Values>> schema(columns);
    return empty(repository, schema.span());
}
template<db::ColumnTypes Values>
db::QueryView<Values> empty(db::QueryRepository<Values>& repository, std::initializer_list<Index<db::Column>> columns)
{
    return empty(repository, std::span<const Index<db::Column>>(columns));
}

template<typename Tag, db::ColumnTypes Values>
db::QueryView<Values> binary(db::QueryRepository<Values>& repository, db::QueryView<Values> lhs, db::QueryView<Values> rhs)
{
    auto data = db::checkout<db::Query<Values, Tag>>(builder<Values>());
    data->lhs = require(repository, lhs);
    data->rhs = require(repository, rhs);
    return db::insert_query(repository, builder<Values>(), *data);
}
template<db::ColumnTypes Values>
db::QueryView<Values> join(db::QueryRepository<Values>& repository, db::QueryView<Values> lhs, db::QueryView<Values> rhs)
{
    return binary<db::QueryJoinTag>(repository, lhs, rhs);
}
template<db::ColumnTypes Values>
db::QueryView<Values> union_(db::QueryRepository<Values>& repository, db::QueryView<Values> lhs, db::QueryView<Values> rhs)
{
    return binary<db::QueryUnionTag>(repository, lhs, rhs);
}
template<db::ColumnTypes Values>
db::QueryView<Values> difference(db::QueryRepository<Values>& repository, db::QueryView<Values> lhs, db::QueryView<Values> rhs)
{
    return binary<db::QueryDifferenceTag>(repository, lhs, rhs);
}

template<typename Tag, db::ColumnTypes Values>
db::QueryView<Values> relabel(db::QueryRepository<Values>& repository, db::QueryView<Values> arg, std::span<const Index<db::Column>> labels)
{
    auto data = db::checkout<db::Query<Values, Tag>>(builder<Values>());
    data->arg = require(repository, arg);
    data->labels.set(labels.begin(), labels.end());
    return db::insert_query(repository, builder<Values>(), *data);
}
template<db::ColumnTypes Values>
db::QueryView<Values> project(db::QueryRepository<Values>& repository, db::QueryView<Values> arg, std::span<const Index<db::Column>> labels)
{
    return relabel<db::QueryProjectTag>(repository, arg, labels);
}
template<db::ColumnTypes Values>
db::QueryView<Values> project(db::QueryRepository<Values>& repository, db::QueryView<Values> arg, std::initializer_list<Index<db::Column>> labels)
{
    return project(repository, arg, std::span<const Index<db::Column>>(labels));
}
template<db::ColumnTypes Values>
db::QueryView<Values> rename(db::QueryRepository<Values>& repository, db::QueryView<Values> arg, std::span<const Index<db::Column>> labels)
{
    return relabel<db::QueryRenameTag>(repository, arg, labels);
}
template<db::ColumnTypes Values>
db::QueryView<Values> rename(db::QueryRepository<Values>& repository, db::QueryView<Values> arg, std::initializer_list<Index<db::Column>> labels)
{
    return rename(repository, arg, std::span<const Index<db::Column>>(labels));
}

template<db::ColumnTypes Values>
db::QueryView<Values> select_equal(db::QueryRepository<Values>& repository, db::QueryView<Values> arg, Index<db::Column> lhs, Index<db::Column> rhs)
{
    auto data = db::checkout<db::Query<Values, db::QuerySelectEqualTag>>(builder<Values>());
    data->arg = require(repository, arg);
    data->lhs_column = lhs;
    data->rhs_column = rhs;
    return db::insert_query(repository, builder<Values>(), *data);
}
template<db::ColumnTypes Values>
db::QueryView<Values> select_encoded(db::QueryRepository<Values>& repository, db::QueryView<Values> arg, Index<db::Column> column, std::span<const std::byte> value)
{
    auto data = db::checkout<db::Query<Values, db::QuerySelectValueTag>>(builder<Values>());
    data->arg = require(repository, arg);
    data->column = column;
    data->constant.set(value.begin(), value.end());
    data->constant_type = arg.columns()[db::column_index(arg.columns(), column)].type;
    return db::insert_query(repository, builder<Values>(), *data);
}
template<db::ColumnTypes Values, db::ColumnValueFor<Values> T>
db::QueryView<Values> select_value(db::QueryRepository<Values>& repository, db::QueryView<Values> arg, Index<db::Column> column, T value)
{
    if (arg.columns()[db::column_index(arg.columns(), column)].type != db::column_type<Values, T>)
        throw std::invalid_argument("Query: constant type does not match selected column.");
    std::array<std::byte, db::ColumnCodec<T>::size> bytes;
    db::ColumnCodec<T>::encode(value, bytes);
    return select_encoded(repository, arg, column, bytes);
}

template<db::ColumnTypes Values>
db::QueryView<Values> distance(db::QueryRepository<Values>& repository,
                               db::QueryView<Values> sources,
                               db::QueryView<Values> edges,
                               db::QueryView<Values> targets,
                               Index<db::Column> distance_column)
{
    auto data = db::checkout<db::Query<Values, db::QueryDistanceTag>>(builder<Values>());
    data->sources = require(repository, sources);
    data->edges = require(repository, edges);
    data->targets = require(repository, targets);
    data->distance_column = distance_column;
    return db::insert_query(repository, builder<Values>(), *data);
}

template<db::ColumnTypes Values>
db::QueryView<Values> generic_join(db::QueryRepository<Values>& repository,
                                   std::span<const db::QueryView<Values>> inputs,
                                   std::span<const Index<db::Column>> variable_order,
                                   std::span<const Index<db::Column>> output_order = {})
{
    auto data = db::checkout<db::Query<Values, db::QueryGenericJoinTag>>(builder<Values>());
    for (const auto input : inputs)
        data->inputs.push_back(require(repository, input));
    data->variable_order.set(variable_order.begin(), variable_order.end());
    data->output_order.set(output_order.begin(), output_order.end());
    return db::insert_query(repository, builder<Values>(), *data);
}
template<db::ColumnTypes Values>
db::QueryView<Values> generic_join(db::QueryRepository<Values>& repository,
                                   std::initializer_list<db::QueryView<Values>> inputs,
                                   std::initializer_list<Index<db::Column>> variable_order,
                                   std::initializer_list<Index<db::Column>> output_order = {})
{
    return generic_join(repository,
                        std::span<const db::QueryView<Values>>(inputs),
                        std::span<const Index<db::Column>>(variable_order),
                        std::span<const Index<db::Column>>(output_order));
}
}  // namespace ygg::tests::qb

#endif
