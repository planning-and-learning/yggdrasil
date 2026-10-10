/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_SYNTAX_QUERY_DATA_HPP_
#define YGG_DATABASE_SYNTAX_QUERY_DATA_HPP_

#include "yggdrasil/core/type_list.hpp"
#include "yggdrasil/core/types_utils.hpp"
#include "yggdrasil/database/semantics/distance.hpp"
#include "yggdrasil/database/semantics/generic_join.hpp"
#include "yggdrasil/database/syntax/query_index.hpp"

#include <cista/containers/variant.h>
#include <cista/containers/vector.h>
#include <concepts>
#include <cstddef>
#include <tuple>
#include <utility>

namespace ygg
{
template<database::ColumnTypes Values>
struct Data<database::Query<Values>>
{
    template<typename Tag>
    using AlternativeIndex = Index<database::Query<Values, Tag>>;
    using Variant = ApplyTypeListT<cista::offset::variant, MapTypeListT<AlternativeIndex, database::QueryConstructorTags>>;

    Index<database::Query<Values>> index;
    Variant variant;

    auto identifying_members() const noexcept { return std::tie(variant); }
    auto cista_members() noexcept { return std::tie(index, variant); }
    auto cista_members() const noexcept { return std::tie(index, variant); }
    void clear()
    {
        std::apply([](auto&... member) { (ygg::clear(member), ...); }, cista_members());
    }
};

template<database::ColumnTypes Values>
struct Data<database::Query<Values, database::QueryInputTag>>
{
    Index<database::Query<Values, database::QueryInputTag>> index;
    size_t input_slot = 0;
    cista::offset::vector<database::ColumnLayout> columns;

    auto identifying_members() const noexcept { return std::tie(input_slot, columns); }
    auto cista_members() noexcept { return std::tie(index, input_slot, columns); }
    auto cista_members() const noexcept { return std::tie(index, input_slot, columns); }
    void clear()
    {
        std::apply([](auto&... member) { (ygg::clear(member), ...); }, cista_members());
    }
};

template<database::ColumnTypes Values>
struct Data<database::Query<Values, database::QueryEmptyTag>>
{
    Index<database::Query<Values, database::QueryEmptyTag>> index;
    cista::offset::vector<database::ColumnLayout> columns;

    auto identifying_members() const noexcept { return std::tie(columns); }
    auto cista_members() noexcept { return std::tie(index, columns); }
    auto cista_members() const noexcept { return std::tie(index, columns); }
    void clear()
    {
        std::apply([](auto&... member) { (ygg::clear(member), ...); }, cista_members());
    }
};

// Prepared schemas and positions are serialized but do not define operator identity.
template<database::ColumnTypes Values>
struct Data<database::Query<Values, database::QueryJoinTag>>
{
    Index<database::Query<Values, database::QueryJoinTag>> index;
    Index<database::Query<Values>> lhs;
    Index<database::Query<Values>> rhs;
    database::JoinPlan<Values> plan;

    auto identifying_members() const noexcept { return std::tie(lhs, rhs); }
    auto cista_members() noexcept { return std::tie(index, lhs, rhs, plan); }
    auto cista_members() const noexcept { return std::tie(index, lhs, rhs, plan); }
    void clear()
    {
        std::apply([](auto&... member) { (ygg::clear(member), ...); }, cista_members());
    }
};

template<database::ColumnTypes Values, typename Tag>
    requires(std::same_as<Tag, database::QueryUnionTag> || std::same_as<Tag, database::QueryDifferenceTag>)
struct Data<database::Query<Values, Tag>>
{
    Index<database::Query<Values, Tag>> index;
    Index<database::Query<Values>> lhs;
    Index<database::Query<Values>> rhs;
    cista::offset::vector<database::ColumnLayout> columns;

    auto identifying_members() const noexcept { return std::tie(lhs, rhs); }
    auto cista_members() noexcept { return std::tie(index, lhs, rhs, columns); }
    auto cista_members() const noexcept { return std::tie(index, lhs, rhs, columns); }
    void clear()
    {
        std::apply([](auto&... member) { (ygg::clear(member), ...); }, cista_members());
    }
};

template<database::ColumnTypes Values>
struct Data<database::Query<Values, database::QueryProjectTag>>
{
    Index<database::Query<Values, database::QueryProjectTag>> index;
    Index<database::Query<Values>> arg;
    cista::offset::vector<Index<database::Column>> labels;
    database::ProjectionPlan<Values> plan;

    auto identifying_members() const noexcept { return std::tie(arg, labels); }
    auto cista_members() noexcept { return std::tie(index, arg, labels, plan); }
    auto cista_members() const noexcept { return std::tie(index, arg, labels, plan); }
    void clear()
    {
        std::apply([](auto&... member) { (ygg::clear(member), ...); }, cista_members());
    }
};

template<database::ColumnTypes Values>
struct Data<database::Query<Values, database::QueryRenameTag>>
{
    Index<database::Query<Values, database::QueryRenameTag>> index;
    Index<database::Query<Values>> arg;
    cista::offset::vector<Index<database::Column>> labels;
    cista::offset::vector<database::ColumnLayout> columns;

    auto identifying_members() const noexcept { return std::tie(arg, labels); }
    auto cista_members() noexcept { return std::tie(index, arg, labels, columns); }
    auto cista_members() const noexcept { return std::tie(index, arg, labels, columns); }
    void clear()
    {
        std::apply([](auto&... member) { (ygg::clear(member), ...); }, cista_members());
    }
};

template<database::ColumnTypes Values>
struct Data<database::Query<Values, database::QuerySelectEqualTag>>
{
    Index<database::Query<Values, database::QuerySelectEqualTag>> index;
    Index<database::Query<Values>> arg;
    Index<database::Column> lhs_column;
    Index<database::Column> rhs_column;
    cista::offset::vector<database::ColumnLayout> columns;
    size_t lhs_position = 0;
    size_t rhs_position = 0;

    auto identifying_members() const noexcept { return std::tie(arg, lhs_column, rhs_column); }
    auto cista_members() noexcept { return std::tie(index, arg, lhs_column, rhs_column, columns, lhs_position, rhs_position); }
    auto cista_members() const noexcept { return std::tie(index, arg, lhs_column, rhs_column, columns, lhs_position, rhs_position); }
    void clear()
    {
        std::apply([](auto&... member) { (ygg::clear(member), ...); }, cista_members());
    }
};

template<database::ColumnTypes Values>
struct Data<database::Query<Values, database::QuerySelectValueTag>>
{
    Index<database::Query<Values, database::QuerySelectValueTag>> index;
    Index<database::Query<Values>> arg;
    Index<database::Column> column;
    cista::offset::vector<std::byte> constant;
    cista::offset::vector<database::ColumnLayout> columns;
    size_t column_position = 0;

    auto identifying_members() const noexcept { return std::tie(arg, column, constant); }
    auto cista_members() noexcept { return std::tie(index, arg, column, constant, columns, column_position); }
    auto cista_members() const noexcept { return std::tie(index, arg, column, constant, columns, column_position); }
    void clear()
    {
        std::apply([](auto&... member) { (ygg::clear(member), ...); }, cista_members());
    }
};

template<database::ColumnTypes Values>
struct Data<database::Query<Values, database::QueryDistanceTag>>
{
    Index<database::Query<Values, database::QueryDistanceTag>> index;
    Index<database::Query<Values>> sources;
    Index<database::Query<Values>> edges;
    Index<database::Query<Values>> targets;
    Index<database::Column> distance_column;
    database::DistancePlan<Values> plan;

    auto identifying_members() const noexcept { return std::tie(sources, edges, targets, distance_column); }
    auto cista_members() noexcept { return std::tie(index, sources, edges, targets, distance_column, plan); }
    auto cista_members() const noexcept { return std::tie(index, sources, edges, targets, distance_column, plan); }
    void clear()
    {
        std::apply([](auto&... member) { (ygg::clear(member), ...); }, cista_members());
    }
};

template<database::ColumnTypes Values>
struct Data<database::Query<Values, database::QueryGenericJoinTag>>
{
    Index<database::Query<Values, database::QueryGenericJoinTag>> index;
    cista::offset::vector<Index<database::Query<Values>>> inputs;
    cista::offset::vector<Index<database::Column>> variable_order;
    cista::offset::vector<Index<database::Column>> output_order;
    database::GenericJoinPlan<Values> plan;

    auto identifying_members() const noexcept { return std::tie(inputs, variable_order, output_order); }
    auto cista_members() noexcept { return std::tie(index, inputs, variable_order, output_order, plan); }
    auto cista_members() const noexcept { return std::tie(index, inputs, variable_order, output_order, plan); }
    void clear()
    {
        std::apply([](auto&... member) { (ygg::clear(member), ...); }, cista_members());
    }
};
}  // namespace ygg

namespace ygg::database
{
namespace detail
{
template<typename Tag, typename DataType, typename F>
void query_children(DataType& data, F&& f)
{
    if constexpr (std::same_as<Tag, QueryInputTag> || std::same_as<Tag, QueryEmptyTag>)
        return;
    else if constexpr (std::same_as<Tag, QueryJoinTag> || std::same_as<Tag, QueryUnionTag> || std::same_as<Tag, QueryDifferenceTag>)
    {
        f(data.lhs);
        f(data.rhs);
    }
    else if constexpr (std::same_as<Tag, QueryDistanceTag>)
    {
        f(data.sources);
        f(data.edges);
        f(data.targets);
    }
    else if constexpr (std::same_as<Tag, QueryGenericJoinTag>)
        for (auto& input : data.inputs)
            f(input);
    else
        f(data.arg);
}
}

template<ColumnTypes Values, typename Tag, typename F>
void for_each_child(const Data<Query<Values, Tag>>& data, F&& f)
{
    detail::query_children<Tag>(data, std::forward<F>(f));
}
template<ColumnTypes Values, typename Tag, typename F>
Data<Query<Values, Tag>> map_children(Data<Query<Values, Tag>> data, F&& f)
{
    detail::query_children<Tag>(data, [&](auto& child) { child = f(child); });
    return data;
}
}  // namespace ygg::database

#endif
