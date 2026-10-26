#ifndef YGG_TESTS_UNIT_DATABASE_QUERY_HELPERS_HPP_
#define YGG_TESTS_UNIT_DATABASE_QUERY_HELPERS_HPP_

#include <array>
#include <initializer_list>
#include <span>
#include <stdexcept>
#include <yggdrasil/database/query/query.hpp>

/// Test-only query construction on top of checkout + insert_query.
namespace ygg::tests::qb
{
/// A schema whose columns all have type T.
template<ygg::ByteCodable T>
Builder<database::Columns> columns_of(std::initializer_list<Index<database::Column>> labels)
{
    Builder<database::Columns> columns;
    columns.template assign<T>(labels);
    return columns;
}

inline database::QueryRepository repository()
{
    static database::QueryRepositoryFactory factory;
    return factory.create();
}

inline database::QueryBuilder& builder()
{
    static database::QueryBuilder storage;
    return storage;
}

inline Index<database::Query<>> require(const database::QueryRepository& repository, database::QueryView<> query)
{
    if (&query.get_repository() != &repository)
        throw std::invalid_argument("Query: operand belongs to another repository.");
    return query.get_index();
}

inline database::QueryView<> input(database::QueryRepository& repository, size_t slot, std::span<const database::ColumnLayout> columns)
{
    auto data = database::checkout<database::Query<database::QueryInputTag>>(builder());
    data->input_slot = slot;
    data->columns.set(columns.begin(), columns.end());
    return database::insert_query(repository, builder(), *data);
}
inline database::QueryView<> input(database::QueryRepository& repository, size_t slot, std::span<const Index<database::Column>> columns)
{
    const Builder<database::Columns> schema(columns);
    return input(repository, slot, schema.span());
}
inline database::QueryView<> input(database::QueryRepository& repository, size_t slot, std::initializer_list<Index<database::Column>> columns)
{ return input(repository, slot, std::span<const Index<database::Column>>(columns)); }

inline database::QueryView<> empty(database::QueryRepository& repository, std::span<const database::ColumnLayout> columns)
{
    auto data = database::checkout<database::Query<database::QueryEmptyTag>>(builder());
    data->columns.set(columns.begin(), columns.end());
    return database::insert_query(repository, builder(), *data);
}
inline database::QueryView<> empty(database::QueryRepository& repository, std::span<const Index<database::Column>> columns)
{
    const Builder<database::Columns> schema(columns);
    return empty(repository, schema.span());
}
inline database::QueryView<> empty(database::QueryRepository& repository, std::initializer_list<Index<database::Column>> columns)
{ return empty(repository, std::span<const Index<database::Column>>(columns)); }

template<typename Tag>
database::QueryView<> binary(database::QueryRepository& repository, database::QueryView<> lhs, database::QueryView<> rhs)
{
    auto data = database::checkout<database::Query<Tag>>(builder());
    data->lhs = require(repository, lhs);
    data->rhs = require(repository, rhs);
    return database::insert_query(repository, builder(), *data);
}
inline database::QueryView<> join(database::QueryRepository& repository, database::QueryView<> lhs, database::QueryView<> rhs)
{ return binary<database::QueryJoinTag>(repository, lhs, rhs); }
inline database::QueryView<> union_(database::QueryRepository& repository, database::QueryView<> lhs, database::QueryView<> rhs)
{ return binary<database::QueryUnionTag>(repository, lhs, rhs); }
inline database::QueryView<> difference(database::QueryRepository& repository, database::QueryView<> lhs, database::QueryView<> rhs)
{ return binary<database::QueryDifferenceTag>(repository, lhs, rhs); }

inline database::QueryView<> semijoin(database::QueryRepository& repository, database::QueryView<> lhs, database::QueryView<> rhs)
{ return binary<database::QuerySemijoinTag>(repository, lhs, rhs); }
inline database::QueryView<> antijoin(database::QueryRepository& repository, database::QueryView<> lhs, database::QueryView<> rhs)
{ return binary<database::QueryAntijoinTag>(repository, lhs, rhs); }

template<typename Tag>
database::QueryView<> relabel(database::QueryRepository& repository, database::QueryView<> arg, std::span<const Index<database::Column>> labels)
{
    auto data = database::checkout<database::Query<Tag>>(builder());
    data->arg = require(repository, arg);
    data->labels.set(labels.begin(), labels.end());
    return database::insert_query(repository, builder(), *data);
}
inline database::QueryView<> project(database::QueryRepository& repository, database::QueryView<> arg, std::span<const Index<database::Column>> labels)
{ return relabel<database::QueryProjectTag>(repository, arg, labels); }
inline database::QueryView<> project(database::QueryRepository& repository, database::QueryView<> arg, std::initializer_list<Index<database::Column>> labels)
{ return project(repository, arg, std::span<const Index<database::Column>>(labels)); }
inline database::QueryView<> rename(database::QueryRepository& repository, database::QueryView<> arg, std::span<const Index<database::Column>> labels)
{ return relabel<database::QueryRenameTag>(repository, arg, labels); }
inline database::QueryView<> rename(database::QueryRepository& repository, database::QueryView<> arg, std::initializer_list<Index<database::Column>> labels)
{ return rename(repository, arg, std::span<const Index<database::Column>>(labels)); }

inline database::QueryView<>
select_equal(database::QueryRepository& repository, database::QueryView<> arg, Index<database::Column> lhs, Index<database::Column> rhs)
{
    auto data = database::checkout<database::Query<database::QuerySelectEqualTag>>(builder());
    data->arg = require(repository, arg);
    data->lhs_column = lhs;
    data->rhs_column = rhs;
    return database::insert_query(repository, builder(), *data);
}
inline database::QueryView<>
select_encoded(database::QueryRepository& repository, database::QueryView<> arg, Index<database::Column> column, std::span<const std::byte> value)
{
    auto data = database::checkout<database::Query<database::QuerySelectValueTag>>(builder());
    data->arg = require(repository, arg);
    data->column = column;
    data->constant.set(value.begin(), value.end());
    data->constant_type = arg.columns()[database::column_index(arg.columns(), column)].type;
    return database::insert_query(repository, builder(), *data);
}
template<ygg::ByteCodable T>
database::QueryView<> select_value(database::QueryRepository& repository, database::QueryView<> arg, Index<database::Column> column, T value)
{
    if (arg.columns()[database::column_index(arg.columns(), column)].type != ygg::type_id<T>)
        throw std::invalid_argument("Query: constant type does not match selected column.");
    std::array<std::byte, ygg::ByteCodec<T>::size> bytes {};
    ygg::ByteCodec<T>::encode(value, bytes);
    return select_encoded(repository, arg, column, bytes);
}

inline database::QueryView<> distance(database::QueryRepository& repository,
                                      database::QueryView<> sources,
                                      database::QueryView<> edges,
                                      database::QueryView<> targets,
                                      Index<database::Column> distance_column)
{
    auto data = database::checkout<database::Query<database::QueryDistanceTag>>(builder());
    data->sources = require(repository, sources);
    data->edges = require(repository, edges);
    data->targets = require(repository, targets);
    data->distance_column = distance_column;
    return database::insert_query(repository, builder(), *data);
}

inline database::QueryView<> generic_join(database::QueryRepository& repository,
                                          std::span<const database::QueryView<>> inputs,
                                          std::span<const Index<database::Column>> variable_order,
                                          std::span<const Index<database::Column>> output_order = {})
{
    auto data = database::checkout<database::Query<database::QueryGenericJoinTag>>(builder());
    for (const auto input : inputs)
        data->inputs.push_back(require(repository, input));
    data->variable_order.set(variable_order.begin(), variable_order.end());
    data->output_order.set(output_order.begin(), output_order.end());
    return database::insert_query(repository, builder(), *data);
}
inline database::QueryView<> generic_join(database::QueryRepository& repository,
                                          std::initializer_list<database::QueryView<>> inputs,
                                          std::initializer_list<Index<database::Column>> variable_order,
                                          std::initializer_list<Index<database::Column>> output_order = {})
{
    return generic_join(repository,
                        std::span<const database::QueryView<>>(inputs),
                        std::span<const Index<database::Column>>(variable_order),
                        std::span<const Index<database::Column>>(output_order));
}
}  // namespace ygg::tests::qb

#endif
