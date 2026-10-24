/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_DETAILS_RELATION_POOL_HPP_
#define YGG_DATABASE_SEMANTICS_DETAILS_RELATION_POOL_HPP_

#include "yggdrasil/database/semantics/relation_pool.hpp"

namespace ygg::database
{

template<ColumnSchema Schema>
UniqueObjectPoolPtr<ygg::Builder<Relation>> RelationPool::allocate(size_t width, Schema columns)
{
    auto& pool = m_pools[width];
    if (!pool)
        pool = std::make_unique<UniqueObjectPool<ygg::Builder<Relation>>>();
    auto relation = pool->get_or_allocate(columns);
    if (relation->m_storage_index == std::numeric_limits<size_t>::max())
        relation->m_storage_index = m_factory.next_index();
    return relation;
}

template<ByteCodable T>
UniqueObjectPoolPtr<ygg::Builder<Relation>> RelationPool::get_or_allocate(std::span<const Index<Column>> columns)
{
    auto typed = ygg::Builder<Columns> {};
    typed.template assign<T>(columns);
    return allocate(typed.row_size(), typed.span());
}

template<ByteCodable T>
UniqueObjectPoolPtr<ygg::Builder<Relation>> RelationPool::get_or_allocate(std::initializer_list<Index<Column>> columns)
{ return get_or_allocate<T>(std::span<const Index<Column>>(columns)); }

}  // namespace ygg::database

#endif
