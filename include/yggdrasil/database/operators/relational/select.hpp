/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_DATABASE_OPERATORS_RELATIONAL_SELECT_HPP_
#define YGG_DATABASE_OPERATORS_RELATIONAL_SELECT_HPP_

#include "yggdrasil/database/operators/relational/common.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace ygg::database
{
/// The predicate receives a typed row borrowing the input and its schema. It must not mutate
/// inputs or output. A throwing predicate can leave a partial output result.
template<RelationViewConcept V, typename Predicate>
    requires std::predicate<Predicate&, Row>
void select(const V& input, Predicate predicate, Builder<Relation>& out);

template<RelationViewConcept V, typename Predicate>
    requires std::predicate<Predicate&, Row>
Builder<Relation> select(const V& input, Predicate predicate);

template<RelationViewConcept V>
void select_equal_columns(const V& input, Index<Column> lhs, Index<Column> rhs, Builder<Relation>& out);

template<RelationViewConcept V>
Builder<Relation> select_equal_columns(const V& input, Index<Column> lhs, Index<Column> rhs);

template<RelationViewConcept V, ByteCodable T>
void select_equal_value(const V& input, Index<Column> column, const T& value, Builder<Relation>& out);

template<RelationViewConcept V, ByteCodable T>
Builder<Relation> select_equal_value(const V& input, Index<Column> column, const T& value);

template<RelationViewConcept V, typename Predicate>
    requires std::predicate<Predicate&, Row>
void select(const V& input, Predicate predicate, Builder<Relation>& out)
{
    detail::prepare_output(out, input.columns().span(), { input.get_storage_address() });
    for (size_t i = 0; i < input.size(); ++i)
        if (std::invoke(predicate, Row(input.row(i), input.columns().span())))
            out.insert(Row(input.row(i), input.columns().span()));
}

template<RelationViewConcept V, typename Predicate>
    requires std::predicate<Predicate&, Row>
Builder<Relation> select(const V& input, Predicate predicate)
{
    auto out = Builder<Relation>(input.columns().span());
    select(input, std::move(predicate), out);
    return out;
}

template<RelationViewConcept V>
void select_equal_columns(const V& input, Index<Column> lhs, Index<Column> rhs, Builder<Relation>& out)
{
    const auto& left = input.columns().span()[input.column_index(lhs)];
    const auto& right = input.columns().span()[input.column_index(rhs)];
    if (left.type != right.type)
        throw std::invalid_argument("Relational operation: selected columns must have the same type.");
    select(input, [=](Row row) { return std::ranges::equal(row.bytes().subspan(left.offset, left.size), row.bytes().subspan(right.offset, right.size)); }, out);
}

template<RelationViewConcept V>
Builder<Relation> select_equal_columns(const V& input, Index<Column> lhs, Index<Column> rhs)
{
    auto out = Builder<Relation>(input.columns().span());
    select_equal_columns(input, lhs, rhs, out);
    return out;
}

template<RelationViewConcept V, ByteCodable T>
void select_equal_value(const V& input, Index<Column> column, const T& value, Builder<Relation>& out)
{
    const auto& field = input.columns().span()[input.column_index(column)];
    if (field.type != type_id<T>)
        throw std::invalid_argument("Relational operation: selected value has an incompatible column type.");
    auto encoded = std::array<std::byte, ByteCodec<T>::size> {};
    ByteCodec<T>::encode(value, encoded);
    select(input, [=](Row row) { return std::ranges::equal(row.bytes().subspan(field.offset, field.size), encoded); }, out);
}

template<RelationViewConcept V, ByteCodable T>
Builder<Relation> select_equal_value(const V& input, Index<Column> column, const T& value)
{
    auto out = Builder<Relation>(input.columns().span());
    select_equal_value(input, column, value, out);
    return out;
}
}  // namespace ygg::database

#endif
