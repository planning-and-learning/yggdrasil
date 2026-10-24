/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_SYNTAX_QUERY_DATA_HPP_
#define YGG_DATABASE_SYNTAX_QUERY_DATA_HPP_

#include "yggdrasil/containers/variant.hpp"
#include "yggdrasil/core/byte_codec.hpp"
#include "yggdrasil/core/type_list.hpp"
#include "yggdrasil/core/types_utils.hpp"
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

    static constexpr auto identifying_members() noexcept { return ygg::Members<&Data::variant> {}; }
    static constexpr auto cista_members() noexcept { return ygg::Members<&Data::index, &Data::variant> {}; }
    void clear() noexcept
    {
        ygg::clear_members(*this);
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

    static constexpr auto identifying_members() noexcept { return ygg::Members<&Data::input_slot, &Data::columns> {}; }
    static constexpr auto cista_members() noexcept { return ygg::Members<&Data::index, &Data::input_slot, &Data::columns> {}; }
    void clear() noexcept
    {
        ygg::clear_members(*this);
    }
};

template<>
struct Data<database::Query<database::QueryEmptyTag>>
{
    Index<database::Query<database::QueryEmptyTag>> index;
    cista::offset::vector<database::ColumnLayout> columns;

    Data() = default;
    explicit Data(std::span<const database::ColumnLayout> columns_) { columns.set(columns_.begin(), columns_.end()); }

    static constexpr auto identifying_members() noexcept { return ygg::Members<&Data::columns> {}; }
    static constexpr auto cista_members() noexcept { return ygg::Members<&Data::index, &Data::columns> {}; }
    void clear() noexcept
    {
        ygg::clear_members(*this);
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

    static constexpr auto identifying_members() noexcept { return ygg::Members<&Data::lhs, &Data::rhs> {}; }
    static constexpr auto cista_members() noexcept { return ygg::Members<&Data::index, &Data::lhs, &Data::rhs, &Data::plan> {}; }
    void clear() noexcept
    {
        ygg::clear_members(*this);
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

    static constexpr auto identifying_members() noexcept { return ygg::Members<&Data::lhs, &Data::rhs> {}; }
    static constexpr auto cista_members() noexcept { return ygg::Members<&Data::index, &Data::lhs, &Data::rhs, &Data::columns> {}; }
    void clear() noexcept
    {
        ygg::clear_members(*this);
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

    static constexpr auto identifying_members() noexcept { return ygg::Members<&Data::arg, &Data::labels> {}; }
    static constexpr auto cista_members() noexcept { return ygg::Members<&Data::index, &Data::arg, &Data::labels, &Data::plan> {}; }
    void clear() noexcept
    {
        ygg::clear_members(*this);
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

    static constexpr auto identifying_members() noexcept { return ygg::Members<&Data::arg, &Data::labels> {}; }
    static constexpr auto cista_members() noexcept { return ygg::Members<&Data::index, &Data::arg, &Data::labels, &Data::columns> {}; }
    void clear() noexcept
    {
        ygg::clear_members(*this);
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

    static constexpr auto identifying_members() noexcept { return ygg::Members<&Data::arg, &Data::lhs_column, &Data::rhs_column> {}; }
    static constexpr auto cista_members() noexcept { return ygg::Members<&Data::index, &Data::arg, &Data::lhs_column, &Data::rhs_column, &Data::columns, &Data::lhs_position, &Data::rhs_position> {}; }
    void clear() noexcept
    {
        ygg::clear_members(*this);
    }
};

template<>
struct Data<database::Query<database::QuerySelectValueTag>>
{
    Index<database::Query<database::QuerySelectValueTag>> index;
    Index<database::Query<>> arg;
    Index<database::Column> column;
    cista::offset::vector<std::byte> constant;
    /// The type of the encoded constant.
    ygg::TypeId constant_type = ygg::TypeId::none;
    cista::offset::vector<database::ColumnLayout> columns;
    size_t column_position = 0;

    Data() = default;
    /// The constant is encoded with the column codec of its type.
    template<ygg::ByteCodable T>
    Data(Index<database::Query<>> arg_, Index<database::Column> column_, T value) : arg(arg_), column(column_), constant_type(ygg::type_id<T>)
    {
        std::array<std::byte, ygg::ByteCodec<T>::size> bytes {};
        ygg::ByteCodec<T>::encode(value, bytes);
        constant.set(bytes.begin(), bytes.end());
    }
    template<typename C, ygg::ByteCodable T>
    Data(View<Index<database::Query<>>, C> arg_, Index<database::Column> column_, T value) : Data(arg_.get_index(), column_, value)
    {
    }

    static constexpr auto identifying_members() noexcept { return ygg::Members<&Data::arg, &Data::column, &Data::constant> {}; }
    static constexpr auto cista_members() noexcept { return ygg::Members<&Data::index, &Data::arg, &Data::column, &Data::constant, &Data::constant_type, &Data::columns, &Data::column_position> {}; }
    void clear() noexcept
    {
        ygg::clear_members(*this);
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
    Data(Index<database::Query<>> sources_, Index<database::Query<>> edges_, Index<database::Query<>> targets_, Index<database::Column> distance_column_) :
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

    static constexpr auto identifying_members() noexcept { return ygg::Members<&Data::sources, &Data::edges, &Data::targets, &Data::distance_column> {}; }
    static constexpr auto cista_members() noexcept { return ygg::Members<&Data::index, &Data::sources, &Data::edges, &Data::targets, &Data::distance_column, &Data::plan> {}; }
    void clear() noexcept
    {
        ygg::clear_members(*this);
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

    static constexpr auto identifying_members() noexcept { return ygg::Members<&Data::inputs, &Data::variable_order, &Data::output_order> {}; }
    static constexpr auto cista_members() noexcept { return ygg::Members<&Data::index, &Data::inputs, &Data::variable_order, &Data::output_order, &Data::plan> {}; }
    void clear() noexcept
    {
        ygg::clear_members(*this);
    }
};
}  // namespace ygg

#endif
