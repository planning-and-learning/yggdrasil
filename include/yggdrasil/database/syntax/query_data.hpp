/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_SYNTAX_QUERY_DATA_HPP_
#define YGG_DATABASE_SYNTAX_QUERY_DATA_HPP_

#include "yggdrasil/containers/variant.hpp"
#include "yggdrasil/core/type_list.hpp"
#include "yggdrasil/core/types_utils.hpp"
#include "yggdrasil/database/semantics/distance.hpp"
#include "yggdrasil/database/semantics/generic_join.hpp"
#include "yggdrasil/ids/byte_codec.hpp"
#include "yggdrasil/database/declarations.hpp"

#include <array>
#include <cista/containers/variant.h>
#include <cista/containers/vector.h>
#include <concepts>
#include <cstddef>
#include <ranges>
#include <span>
#include <type_traits>
#include <tuple>
#include <utility>
#include <vector>

namespace ygg
{
template<>
struct Data<database::Query<>>
{
    using Variant = IndexVariant<MapTypeListT<database::Query, database::QueryConstructorTags>>;

    Index<database::Query<>> index;
    Variant variant;

    Data() = default;
    explicit Data(Variant variant_) : index(), variant(std::move(variant_)) {}
    /// The erased record of an interned concrete query.
    template<typename Tag, typename C>
    explicit Data(View<Index<database::Query<Tag>>, C> concrete) : variant(concrete.get_index())
    {
    }

    auto identifying_members() const noexcept { return std::tie(variant); }
    auto cista_members() noexcept { return std::tie(index, variant); }
    auto cista_members() const noexcept { return std::tie(index, variant); }
    void clear() noexcept
    {
        std::apply([](auto&... member) { (ygg::clear(member), ...); }, cista_members());
    }
};

template<>
struct Data<database::Query<database::QueryInputTag>>
{
    Index<database::Query<database::QueryInputTag>> index;
    size_t input_slot = 0;
    cista::offset::vector<database::ColumnLayout> columns;

    Data() = default;
    Data(size_t input_slot_, std::span<const database::ColumnLayout> columns_) : input_slot(input_slot_) { columns.set(columns_.begin(), columns_.end()); }

    auto identifying_members() const noexcept { return std::tie(input_slot, columns); }
    auto cista_members() noexcept { return std::tie(index, input_slot, columns); }
    auto cista_members() const noexcept { return std::tie(index, input_slot, columns); }
    void clear() noexcept
    {
        std::apply([](auto&... member) { (ygg::clear(member), ...); }, cista_members());
    }
};

template<>
struct Data<database::Query<database::QueryEmptyTag>>
{
    Index<database::Query<database::QueryEmptyTag>> index;
    cista::offset::vector<database::ColumnLayout> columns;

    Data() = default;
    explicit Data(std::span<const database::ColumnLayout> columns_) { columns.set(columns_.begin(), columns_.end()); }

    auto identifying_members() const noexcept { return std::tie(columns); }
    auto cista_members() noexcept { return std::tie(index, columns); }
    auto cista_members() const noexcept { return std::tie(index, columns); }
    void clear() noexcept
    {
        std::apply([](auto&... member) { (ygg::clear(member), ...); }, cista_members());
    }
};

// Prepared schemas and positions are serialized but do not define operator identity.
template<typename Tag>
    requires(std::same_as<Tag, database::QueryJoinTag> || std::same_as<Tag, database::QuerySemijoinTag> || std::same_as<Tag, database::QueryAntijoinTag>)
struct Data<database::Query<Tag>>
{
    Index<database::Query<Tag>> index;
    Index<database::Query<>> lhs;
    Index<database::Query<>> rhs;
    database::JoinPlan plan;

    Data() = default;
    Data(Index<database::Query<>> lhs_, Index<database::Query<>> rhs_) : lhs(lhs_), rhs(rhs_) {}
    template<typename C>
    Data(View<Index<database::Query<>>, C> lhs_, View<Index<database::Query<>>, C> rhs_) : Data(lhs_.get_index(), rhs_.get_index())
    {
    }

    auto identifying_members() const noexcept { return std::tie(lhs, rhs); }
    auto cista_members() noexcept { return std::tie(index, lhs, rhs, plan); }
    auto cista_members() const noexcept { return std::tie(index, lhs, rhs, plan); }
    void clear() noexcept
    {
        std::apply([](auto&... member) { (ygg::clear(member), ...); }, cista_members());
    }
};

template<typename Tag>
    requires(std::same_as<Tag, database::QueryUnionTag> || std::same_as<Tag, database::QueryDifferenceTag>)
struct Data<database::Query<Tag>>
{
    Index<database::Query<Tag>> index;
    Index<database::Query<>> lhs;
    Index<database::Query<>> rhs;
    cista::offset::vector<database::ColumnLayout> columns;

    Data() = default;
    Data(Index<database::Query<>> lhs_, Index<database::Query<>> rhs_) : lhs(lhs_), rhs(rhs_) {}
    template<typename C>
    Data(View<Index<database::Query<>>, C> lhs_, View<Index<database::Query<>>, C> rhs_) : Data(lhs_.get_index(), rhs_.get_index())
    {
    }

    auto identifying_members() const noexcept { return std::tie(lhs, rhs); }
    auto cista_members() noexcept { return std::tie(index, lhs, rhs, columns); }
    auto cista_members() const noexcept { return std::tie(index, lhs, rhs, columns); }
    void clear() noexcept
    {
        std::apply([](auto&... member) { (ygg::clear(member), ...); }, cista_members());
    }
};

template<>
struct Data<database::Query<database::QueryProjectTag>>
{
    Index<database::Query<database::QueryProjectTag>> index;
    Index<database::Query<>> arg;
    cista::offset::vector<Index<database::Column>> labels;
    database::ProjectionPlan plan;

    Data() = default;
    Data(Index<database::Query<>> arg_, std::span<const Index<database::Column>> labels_) : arg(arg_) { labels.set(labels_.begin(), labels_.end()); }
    template<typename C>
    Data(View<Index<database::Query<>>, C> arg_, std::span<const Index<database::Column>> labels_) : Data(arg_.get_index(), labels_)
    {
    }

    auto identifying_members() const noexcept { return std::tie(arg, labels); }
    auto cista_members() noexcept { return std::tie(index, arg, labels, plan); }
    auto cista_members() const noexcept { return std::tie(index, arg, labels, plan); }
    void clear() noexcept
    {
        std::apply([](auto&... member) { (ygg::clear(member), ...); }, cista_members());
    }
};

template<>
struct Data<database::Query<database::QueryRenameTag>>
{
    Index<database::Query<database::QueryRenameTag>> index;
    Index<database::Query<>> arg;
    cista::offset::vector<Index<database::Column>> labels;
    cista::offset::vector<database::ColumnLayout> columns;

    Data() = default;
    Data(Index<database::Query<>> arg_, std::span<const Index<database::Column>> labels_) : arg(arg_) { labels.set(labels_.begin(), labels_.end()); }
    template<typename C>
    Data(View<Index<database::Query<>>, C> arg_, std::span<const Index<database::Column>> labels_) : Data(arg_.get_index(), labels_)
    {
    }

    auto identifying_members() const noexcept { return std::tie(arg, labels); }
    auto cista_members() noexcept { return std::tie(index, arg, labels, columns); }
    auto cista_members() const noexcept { return std::tie(index, arg, labels, columns); }
    void clear() noexcept
    {
        std::apply([](auto&... member) { (ygg::clear(member), ...); }, cista_members());
    }
};

template<>
struct Data<database::Query<database::QuerySelectEqualTag>>
{
    Index<database::Query<database::QuerySelectEqualTag>> index;
    Index<database::Query<>> arg;
    Index<database::Column> lhs_column;
    Index<database::Column> rhs_column;
    cista::offset::vector<database::ColumnLayout> columns;
    size_t lhs_position = 0;
    size_t rhs_position = 0;

    Data() = default;
    Data(Index<database::Query<>> arg_, Index<database::Column> lhs_column_, Index<database::Column> rhs_column_) :
        arg(arg_),
        lhs_column(lhs_column_),
        rhs_column(rhs_column_)
    {
    }
    template<typename C>
    Data(View<Index<database::Query<>>, C> arg_, Index<database::Column> lhs_column_, Index<database::Column> rhs_column_) :
        Data(arg_.get_index(), lhs_column_, rhs_column_)
    {
    }

    auto identifying_members() const noexcept { return std::tie(arg, lhs_column, rhs_column); }
    auto cista_members() noexcept { return std::tie(index, arg, lhs_column, rhs_column, columns, lhs_position, rhs_position); }
    auto cista_members() const noexcept { return std::tie(index, arg, lhs_column, rhs_column, columns, lhs_position, rhs_position); }
    void clear() noexcept
    {
        std::apply([](auto&... member) { (ygg::clear(member), ...); }, cista_members());
    }
};

template<>
struct Data<database::Query<database::QuerySelectValueTag>>
{
    Index<database::Query<database::QuerySelectValueTag>> index;
    Index<database::Query<>> arg;
    Index<database::Column> column;
    cista::offset::vector<std::byte> constant;
    /// The registered type ordinal of the encoded constant.
    ygg::TypeId constant_type = 0;
    cista::offset::vector<database::ColumnLayout> columns;
    size_t column_position = 0;

    Data() = default;
    /// The constant is encoded with the column codec of its type.
    template<ygg::ByteCodable T>
    Data(Index<database::Query<>> arg_, Index<database::Column> column_, T value) :
        arg(arg_),
        column(column_),
        constant_type(ygg::type_id<T>)
    {
        std::array<std::byte, ygg::ByteCodec<T>::size> bytes;
        ygg::ByteCodec<T>::encode(value, bytes);
        constant.set(bytes.begin(), bytes.end());
    }
    template<typename C, ygg::ByteCodable T>
    Data(View<Index<database::Query<>>, C> arg_, Index<database::Column> column_, T value) : Data(arg_.get_index(), column_, value)
    {
    }

    auto identifying_members() const noexcept { return std::tie(arg, column, constant); }
    auto cista_members() noexcept { return std::tie(index, arg, column, constant, constant_type, columns, column_position); }
    auto cista_members() const noexcept { return std::tie(index, arg, column, constant, constant_type, columns, column_position); }
    void clear() noexcept
    {
        std::apply([](auto&... member) { (ygg::clear(member), ...); }, cista_members());
    }
};

template<>
struct Data<database::Query<database::QueryDistanceTag>>
{
    Index<database::Query<database::QueryDistanceTag>> index;
    Index<database::Query<>> sources;
    Index<database::Query<>> edges;
    Index<database::Query<>> targets;
    Index<database::Column> distance_column;
    database::DistancePlan plan;

    Data() = default;
    Data(Index<database::Query<>> sources_,
         Index<database::Query<>> edges_,
         Index<database::Query<>> targets_,
         Index<database::Column> distance_column_) :
        sources(sources_),
        edges(edges_),
        targets(targets_),
        distance_column(distance_column_)
    {
    }
    template<typename C>
    Data(View<Index<database::Query<>>, C> sources_,
         View<Index<database::Query<>>, C> edges_,
         View<Index<database::Query<>>, C> targets_,
         Index<database::Column> distance_column_) :
        Data(sources_.get_index(), edges_.get_index(), targets_.get_index(), distance_column_)
    {
    }

    auto identifying_members() const noexcept { return std::tie(sources, edges, targets, distance_column); }
    auto cista_members() noexcept { return std::tie(index, sources, edges, targets, distance_column, plan); }
    auto cista_members() const noexcept { return std::tie(index, sources, edges, targets, distance_column, plan); }
    void clear() noexcept
    {
        std::apply([](auto&... member) { (ygg::clear(member), ...); }, cista_members());
    }
};

template<>
struct Data<database::Query<database::QueryGenericJoinTag>>
{
    Index<database::Query<database::QueryGenericJoinTag>> index;
    cista::offset::vector<Index<database::Query<>>> inputs;
    cista::offset::vector<Index<database::Column>> variable_order;
    cista::offset::vector<Index<database::Column>> output_order;
    database::GenericJoinPlan plan;

    Data() = default;
    /// An empty output order is canonicalized on insertion.
    Data(std::span<const Index<database::Query<>>> inputs_,
         std::span<const Index<database::Column>> variable_order_,
         std::span<const Index<database::Column>> output_order_ = {}) :
        index()
    {
        inputs.set(inputs_.begin(), inputs_.end());
        variable_order.set(variable_order_.begin(), variable_order_.end());
        output_order.set(output_order_.begin(), output_order_.end());
    }
    template<typename C>
    Data(const std::vector<::ygg::View<Index<database::Query<>>, C>>& inputs_,
         std::span<const Index<database::Column>> variable_order_,
         std::span<const Index<database::Column>> output_order_ = {}) :
        index()
    {
        set(inputs_, inputs);
        variable_order.set(variable_order_.begin(), variable_order_.end());
        output_order.set(output_order_.begin(), output_order_.end());
    }

    auto identifying_members() const noexcept { return std::tie(inputs, variable_order, output_order); }
    auto cista_members() noexcept { return std::tie(index, inputs, variable_order, output_order, plan); }
    auto cista_members() const noexcept { return std::tie(index, inputs, variable_order, output_order, plan); }
    void clear() noexcept
    {
        std::apply([](auto&... member) { (ygg::clear(member), ...); }, cista_members());
    }
};
}  // namespace ygg


#endif
