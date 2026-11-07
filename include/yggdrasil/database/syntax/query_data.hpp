/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_SYNTAX_QUERY_DATA_HPP_
#define YGG_DATABASE_SYNTAX_QUERY_DATA_HPP_

#include <urd/elements/flat.hpp>
#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>
#include "yggdrasil/database/byte_codec.hpp"
#include <boost/mp11.hpp>
#include "yggdrasil/database/declarations.hpp"
#include "yggdrasil/database/semantics/distance.hpp"
#include "yggdrasil/database/semantics/generic_join.hpp"

#include <array>
#include <cista/containers/variant.h>
#include <cista/containers/vector.h>
#include <concepts>
#include <cstddef>
#include <ranges>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace urd
{
template<>
struct data<ygg::database::Query<>>
{
    using Variant = urd::index_variant<boost::mp11::mp_transform<ygg::database::Query, ygg::database::QueryConstructorTags>>;

    urd::index<ygg::database::Query<>> index;
    Variant variant;

    data() = default;
    explicit data(Variant variant_) : index(), variant(std::move(variant_)) {}

    static constexpr auto identifying_members() noexcept { return urd::Members<&data::variant> {}; }
    static constexpr auto cista_members() noexcept { return urd::Members<&data::index, &data::variant> {}; }
};

template<>
struct data<ygg::database::Query<ygg::database::QueryInputTag>>
{
    urd::index<ygg::database::Query<ygg::database::QueryInputTag>> index;
    size_t input_slot = 0;
    cista::offset::vector<ygg::database::ColumnLayout> columns;

    data() = default;
    data(size_t input_slot_, std::span<const ygg::database::ColumnLayout> columns_) : input_slot(input_slot_) { columns.set(columns_.begin(), columns_.end()); }

    static constexpr auto identifying_members() noexcept { return urd::Members<&data::input_slot, &data::columns> {}; }
    static constexpr auto cista_members() noexcept { return urd::Members<&data::index, &data::input_slot, &data::columns> {}; }
};

template<>
struct data<ygg::database::Query<ygg::database::QueryEmptyTag>>
{
    urd::index<ygg::database::Query<ygg::database::QueryEmptyTag>> index;
    cista::offset::vector<ygg::database::ColumnLayout> columns;

    data() = default;
    explicit data(std::span<const ygg::database::ColumnLayout> columns_) { columns.set(columns_.begin(), columns_.end()); }

    static constexpr auto identifying_members() noexcept { return urd::Members<&data::columns> {}; }
    static constexpr auto cista_members() noexcept { return urd::Members<&data::index, &data::columns> {}; }
};

// Prepared schemas and positions are serialized but do not define operator identity.
template<typename Tag>
    requires(std::same_as<Tag, ygg::database::QueryJoinTag> || std::same_as<Tag, ygg::database::QuerySemijoinTag> || std::same_as<Tag, ygg::database::QueryAntijoinTag>)
struct data<ygg::database::Query<Tag>>
{
    urd::index<ygg::database::Query<Tag>> index;
    urd::index<ygg::database::Query<>> lhs;
    urd::index<ygg::database::Query<>> rhs;
    ygg::database::JoinPlan plan;

    data() = default;
    data(urd::index<ygg::database::Query<>> lhs_, urd::index<ygg::database::Query<>> rhs_) : lhs(lhs_), rhs(rhs_) {}

    static constexpr auto identifying_members() noexcept { return urd::Members<&data::lhs, &data::rhs> {}; }
    static constexpr auto cista_members() noexcept { return urd::Members<&data::index, &data::lhs, &data::rhs, &data::plan> {}; }
};

template<typename Tag>
    requires(std::same_as<Tag, ygg::database::QueryUnionTag> || std::same_as<Tag, ygg::database::QueryDifferenceTag>)
struct data<ygg::database::Query<Tag>>
{
    urd::index<ygg::database::Query<Tag>> index;
    urd::index<ygg::database::Query<>> lhs;
    urd::index<ygg::database::Query<>> rhs;
    cista::offset::vector<ygg::database::ColumnLayout> columns;

    data() = default;
    data(urd::index<ygg::database::Query<>> lhs_, urd::index<ygg::database::Query<>> rhs_) : lhs(lhs_), rhs(rhs_) {}

    static constexpr auto identifying_members() noexcept { return urd::Members<&data::lhs, &data::rhs> {}; }
    static constexpr auto cista_members() noexcept { return urd::Members<&data::index, &data::lhs, &data::rhs, &data::columns> {}; }
};

template<>
struct data<ygg::database::Query<ygg::database::QueryProjectTag>>
{
    urd::index<ygg::database::Query<ygg::database::QueryProjectTag>> index;
    urd::index<ygg::database::Query<>> arg;
    cista::offset::vector<urd::index<ygg::database::Column>> labels;
    ygg::database::ProjectionPlan plan;

    data() = default;
    data(urd::index<ygg::database::Query<>> arg_, std::span<const urd::index<ygg::database::Column>> labels_) : arg(arg_) { labels.set(labels_.begin(), labels_.end()); }

    static constexpr auto identifying_members() noexcept { return urd::Members<&data::arg, &data::labels> {}; }
    static constexpr auto cista_members() noexcept { return urd::Members<&data::index, &data::arg, &data::labels, &data::plan> {}; }
};

template<>
struct data<ygg::database::Query<ygg::database::QueryRenameTag>>
{
    urd::index<ygg::database::Query<ygg::database::QueryRenameTag>> index;
    urd::index<ygg::database::Query<>> arg;
    cista::offset::vector<urd::index<ygg::database::Column>> labels;
    cista::offset::vector<ygg::database::ColumnLayout> columns;

    data() = default;
    data(urd::index<ygg::database::Query<>> arg_, std::span<const urd::index<ygg::database::Column>> labels_) : arg(arg_) { labels.set(labels_.begin(), labels_.end()); }

    static constexpr auto identifying_members() noexcept { return urd::Members<&data::arg, &data::labels> {}; }
    static constexpr auto cista_members() noexcept { return urd::Members<&data::index, &data::arg, &data::labels, &data::columns> {}; }
};

template<>
struct data<ygg::database::Query<ygg::database::QuerySelectEqualTag>>
{
    urd::index<ygg::database::Query<ygg::database::QuerySelectEqualTag>> index;
    urd::index<ygg::database::Query<>> arg;
    urd::index<ygg::database::Column> lhs_column;
    urd::index<ygg::database::Column> rhs_column;
    cista::offset::vector<ygg::database::ColumnLayout> columns;
    size_t lhs_position = 0;
    size_t rhs_position = 0;

    data() = default;
    data(urd::index<ygg::database::Query<>> arg_, urd::index<ygg::database::Column> lhs_column_, urd::index<ygg::database::Column> rhs_column_) :
        arg(arg_),
        lhs_column(lhs_column_),
        rhs_column(rhs_column_)
    {
    }

    static constexpr auto identifying_members() noexcept { return urd::Members<&data::arg, &data::lhs_column, &data::rhs_column> {}; }
    static constexpr auto cista_members() noexcept
    { return urd::Members<&data::index, &data::arg, &data::lhs_column, &data::rhs_column, &data::columns, &data::lhs_position, &data::rhs_position> {}; }
};

template<>
struct data<ygg::database::Query<ygg::database::QuerySelectValueTag>>
{
    urd::index<ygg::database::Query<ygg::database::QuerySelectValueTag>> index;
    urd::index<ygg::database::Query<>> arg;
    urd::index<ygg::database::Column> column;
    cista::offset::vector<std::byte> constant;
    /// The type of the encoded constant.
    ygg::TypeId constant_type = ygg::TypeId::none;
    cista::offset::vector<ygg::database::ColumnLayout> columns;
    size_t column_position = 0;

    data() = default;
    /// The constant is encoded with the column codec of its type.
    template<ygg::ByteCodable T>
    data(urd::index<ygg::database::Query<>> arg_, urd::index<ygg::database::Column> column_, T value) : arg(arg_), column(column_), constant_type(ygg::type_id<T>)
    {
        std::array<std::byte, ygg::ByteCodec<T>::size> bytes {};
        ygg::ByteCodec<T>::encode(value, bytes);
        constant.set(bytes.begin(), bytes.end());
    }

    static constexpr auto identifying_members() noexcept { return urd::Members<&data::arg, &data::column, &data::constant> {}; }
    static constexpr auto cista_members() noexcept
    { return urd::Members<&data::index, &data::arg, &data::column, &data::constant, &data::constant_type, &data::columns, &data::column_position> {}; }
};

template<>
struct data<ygg::database::Query<ygg::database::QueryDistanceTag>>
{
    urd::index<ygg::database::Query<ygg::database::QueryDistanceTag>> index;
    urd::index<ygg::database::Query<>> sources;
    urd::index<ygg::database::Query<>> edges;
    urd::index<ygg::database::Query<>> targets;
    urd::index<ygg::database::Column> distance_column;
    ygg::database::DistancePlan plan;

    data() = default;
    data(urd::index<ygg::database::Query<>> sources_, urd::index<ygg::database::Query<>> edges_, urd::index<ygg::database::Query<>> targets_, urd::index<ygg::database::Column> distance_column_) :
        sources(sources_),
        edges(edges_),
        targets(targets_),
        distance_column(distance_column_)
    {
    }

    static constexpr auto identifying_members() noexcept { return urd::Members<&data::sources, &data::edges, &data::targets, &data::distance_column> {}; }
    static constexpr auto cista_members() noexcept
    { return urd::Members<&data::index, &data::sources, &data::edges, &data::targets, &data::distance_column, &data::plan> {}; }
};

template<>
struct data<ygg::database::Query<ygg::database::QueryGenericJoinTag>>
{
    urd::index<ygg::database::Query<ygg::database::QueryGenericJoinTag>> index;
    cista::offset::vector<urd::index<ygg::database::Query<>>> inputs;
    cista::offset::vector<urd::index<ygg::database::Column>> variable_order;
    cista::offset::vector<urd::index<ygg::database::Column>> output_order;
    ygg::database::GenericJoinPlan plan;

    data() = default;
    /// An empty output order is canonicalized on insertion.
    data(std::span<const urd::index<ygg::database::Query<>>> inputs_,
         std::span<const urd::index<ygg::database::Column>> variable_order_,
         std::span<const urd::index<ygg::database::Column>> output_order_ = {}) :
        index()
    {
        inputs.set(inputs_.begin(), inputs_.end());
        variable_order.set(variable_order_.begin(), variable_order_.end());
        output_order.set(output_order_.begin(), output_order_.end());
    }

    static constexpr auto identifying_members() noexcept { return urd::Members<&data::inputs, &data::variable_order, &data::output_order> {}; }
    static constexpr auto cista_members() noexcept
    { return urd::Members<&data::index, &data::inputs, &data::variable_order, &data::output_order, &data::plan> {}; }
};
}  // namespace urd

#endif
