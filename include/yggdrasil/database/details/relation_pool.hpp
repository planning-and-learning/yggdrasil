/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_DETAILS_RELATION_POOL_HPP_
#define YGG_DATABASE_DETAILS_RELATION_POOL_HPP_

#include "yggdrasil/database/relation_pool.hpp"

namespace ygg::database
{

template<ColumnTypes Values>
RelationPool<Values> RelationPoolFactory<Values>::create_pool()
{
    return RelationPool<Values>(*this);
}

template<ColumnTypes Values>
UniqueObjectPoolPtr<ygg::Builder<Relation<Values>>> RelationPool<Values>::get_or_allocate(std::span<const ColumnLayout> columns)
{
    validate_columns<Values>(columns);
    const auto width = row_size(columns);
    auto relation = m_pools[width].get_or_allocate(columns);
    if (relation->m_storage_index == std::numeric_limits<size_t>::max())
        relation->m_storage_index = m_factory.next_index();
    return relation;
}

template<ColumnTypes Values>
UniqueObjectPoolPtr<ygg::Builder<Relation<Values>>> RelationPool<Values>::get_or_allocate(std::span<const Index<Column>> columns)
{
    detail::validate_column_labels(columns);
    const auto field_width = column_size<Values>(0);
    if (columns.size() > std::numeric_limits<size_t>::max() / field_width)
        throw std::length_error("Columns: row byte width exceeds addressable memory.");
    const auto width = columns.size() * field_width;
    auto relation = m_pools[width].get_or_allocate(columns);
    if (relation->m_storage_index == std::numeric_limits<size_t>::max())
        relation->m_storage_index = m_factory.next_index();
    return relation;
}

template<ColumnTypes Values>
UniqueObjectPoolPtr<ygg::Builder<Relation<Values>>> RelationPool<Values>::get_or_allocate(std::initializer_list<Index<Column>> columns)
{
    return get_or_allocate(std::span<const Index<Column>>(columns));
}

}  // namespace ygg::database

#endif
