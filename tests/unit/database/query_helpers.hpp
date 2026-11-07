#ifndef YGG_TESTS_UNIT_DATABASE_QUERY_HELPERS_HPP_
#define YGG_TESTS_UNIT_DATABASE_QUERY_HELPERS_HPP_

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <map>
#include <initializer_list>
#include <span>
#include <stdexcept>
#include <yggdrasil/database/semantics/relation_repository.hpp>
#include <yggdrasil/database/syntax/query.hpp>

/// Test-only query construction on top of checkout + insert_query.
namespace ygg::tests::qb
{
/// A schema whose columns all have type T.
template<ygg::ByteCodable T>
urd::builder<database::Columns> columns_of(std::initializer_list<urd::index<database::Column>> labels)
{
    urd::builder<database::Columns> columns;
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

inline urd::index<database::Query<>> require(const database::QueryRepository& repository, database::QueryView<> query)
{
    if (&get_repository(query.context()) != &repository)
        throw std::invalid_argument("Query: operand belongs to another repository.");
    return query.index();
}

inline database::QueryView<> input(database::QueryRepository& repository, size_t slot, std::span<const database::ColumnLayout> columns)
{
    auto data = database::checkout<database::Query<database::QueryInputTag>>(builder());
    data->input_slot = slot;
    data->columns.set(columns.begin(), columns.end());
    return database::insert_query(repository, builder(), *data);
}
inline database::QueryView<> input(database::QueryRepository& repository, size_t slot, std::span<const urd::index<database::Column>> columns)
{
    const urd::builder<database::Columns> schema(columns);
    return input(repository, slot, schema.span());
}
inline database::QueryView<> input(database::QueryRepository& repository, size_t slot, std::initializer_list<urd::index<database::Column>> columns)
{ return input(repository, slot, std::span<const urd::index<database::Column>>(columns)); }

inline database::QueryView<> empty(database::QueryRepository& repository, std::span<const database::ColumnLayout> columns)
{
    auto data = database::checkout<database::Query<database::QueryEmptyTag>>(builder());
    data->columns.set(columns.begin(), columns.end());
    return database::insert_query(repository, builder(), *data);
}
inline database::QueryView<> empty(database::QueryRepository& repository, std::span<const urd::index<database::Column>> columns)
{
    const urd::builder<database::Columns> schema(columns);
    return empty(repository, schema.span());
}
inline database::QueryView<> empty(database::QueryRepository& repository, std::initializer_list<urd::index<database::Column>> columns)
{ return empty(repository, std::span<const urd::index<database::Column>>(columns)); }

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
database::QueryView<> relabel(database::QueryRepository& repository, database::QueryView<> arg, std::span<const urd::index<database::Column>> labels)
{
    auto data = database::checkout<database::Query<Tag>>(builder());
    data->arg = require(repository, arg);
    data->labels.set(labels.begin(), labels.end());
    return database::insert_query(repository, builder(), *data);
}
inline database::QueryView<> project(database::QueryRepository& repository, database::QueryView<> arg, std::span<const urd::index<database::Column>> labels)
{ return relabel<database::QueryProjectTag>(repository, arg, labels); }
inline database::QueryView<> project(database::QueryRepository& repository, database::QueryView<> arg, std::initializer_list<urd::index<database::Column>> labels)
{ return project(repository, arg, std::span<const urd::index<database::Column>>(labels)); }
inline database::QueryView<> rename(database::QueryRepository& repository, database::QueryView<> arg, std::span<const urd::index<database::Column>> labels)
{ return relabel<database::QueryRenameTag>(repository, arg, labels); }
inline database::QueryView<> rename(database::QueryRepository& repository, database::QueryView<> arg, std::initializer_list<urd::index<database::Column>> labels)
{ return rename(repository, arg, std::span<const urd::index<database::Column>>(labels)); }

inline database::QueryView<>
select_equal(database::QueryRepository& repository, database::QueryView<> arg, urd::index<database::Column> lhs, urd::index<database::Column> rhs)
{
    auto data = database::checkout<database::Query<database::QuerySelectEqualTag>>(builder());
    data->arg = require(repository, arg);
    data->lhs_column = lhs;
    data->rhs_column = rhs;
    return database::insert_query(repository, builder(), *data);
}
inline database::QueryView<>
select_encoded(database::QueryRepository& repository, database::QueryView<> arg, urd::index<database::Column> column, std::span<const std::byte> value)
{
    auto data = database::checkout<database::Query<database::QuerySelectValueTag>>(builder());
    data->arg = require(repository, arg);
    data->column = column;
    data->constant.set(value.begin(), value.end());
    data->constant_type = arg.columns()[database::column_index(arg.columns(), column)].type;
    return database::insert_query(repository, builder(), *data);
}
template<ygg::ByteCodable T>
database::QueryView<> select_value(database::QueryRepository& repository, database::QueryView<> arg, urd::index<database::Column> column, T value)
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
                                      urd::index<database::Column> distance_column)
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
                                          std::span<const urd::index<database::Column>> variable_order,
                                          std::span<const urd::index<database::Column>> output_order = {})
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
                                          std::initializer_list<urd::index<database::Column>> variable_order,
                                          std::initializer_list<urd::index<database::Column>> output_order = {})
{
    return generic_join(repository,
                        std::span<const database::QueryView<>>(inputs),
                        std::span<const urd::index<database::Column>>(variable_order),
                        std::span<const urd::index<database::Column>>(output_order));
}
}  // namespace ygg::tests::qb

/// Shared by the min-distance tests: relations borrowed as views, rows decoded to uint_t values, reference distances.
namespace ygg::tests::distances
{
inline database::BorrowedRelationView borrow(const urd::builder<database::Relation>& relation)
{
    static const auto context = database::RelationRepositoryFactory {}.create();
    return { &relation, &context };
}

inline urd::Store<urd::sequence<database::BorrowedRelationView>> borrow_all(const urd::Store<urd::sequence<urd::builder<database::Relation>>>& relations)
{
    urd::Store<urd::sequence<database::BorrowedRelationView>> views;
    views.reserve(relations.size());
    for (const auto& relation : relations)
        views.push_back(borrow(relation));
    return views;
}

inline urd::Store<urd::sequence<uint_t>> decode(std::span<const std::byte> bytes, std::span<const database::ColumnLayout> columns)
{
    urd::Store<urd::sequence<uint_t>> values;
    values.reserve(columns.size());
    for (size_t c = 0; c < columns.size(); ++c)
        values.push_back(database::Row(bytes, columns).get<uint_t>(c));
    return values;
}

/// Half the distance to the targets in a supergraph of every state: consistent in all of them.
struct HalfDistance
{
    std::map<urd::Store<urd::sequence<uint_t>>, uint_t> distances;
    std::span<const database::ColumnLayout> columns;

    [[nodiscard]] uint_t estimate(std::span<const std::byte> vertex) const
    {
        const auto found = distances.find(decode(vertex, columns));
        return found == distances.end() ? std::numeric_limits<uint_t>::max() : found->second / 2;
    }
};

/// Backward breadth-first distances to the targets over materialized edges.
inline std::map<urd::Store<urd::sequence<uint_t>>, uint_t> distances_to(const urd::builder<database::Relation>& edges, const urd::builder<database::Relation>& targets, size_t k)
{
    std::map<urd::Store<urd::sequence<uint_t>>, uint_t> result;
    urd::Store<urd::sequence<urd::Store<urd::sequence<uint_t>>>> frontier;
    for (size_t i = 0; i < targets.size(); ++i)
    {
        const auto vertex = decode(targets.row(i), targets.columns().span());
        if (result.emplace(vertex, 0).second)
            frontier.push_back(vertex);
    }
    for (uint_t level = 1; !frontier.empty(); ++level)
    {
        urd::Store<urd::sequence<urd::Store<urd::sequence<uint_t>>>> next;
        for (size_t i = 0; i < edges.size(); ++i)
        {
            const auto edge = decode(edges.row(i), edges.columns().span());
            const auto from = urd::Store<urd::sequence<uint_t>>(edge.begin(), edge.begin() + static_cast<std::ptrdiff_t>(k));
            const auto to = urd::Store<urd::sequence<uint_t>>(edge.begin() + static_cast<std::ptrdiff_t>(k), edge.end());
            if (std::ranges::find(frontier, to) != frontier.end() && result.emplace(from, level).second)
                next.push_back(from);
        }
        frontier = std::move(next);
    }
    return result;
}
}  // namespace ygg::tests::distances

#endif
