/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_DETAILS_RELATION_POOL_HPP_
#define YGG_DATABASE_DETAILS_RELATION_POOL_HPP_

#include "yggdrasil/database/relation_pool.hpp"

namespace ygg::database
{

template<TriviallyCopyable T>
RelationPool<T> RelationPoolFactory<T>::create_pool()
{
    return RelationPool<T>(*this);
}

template<TriviallyCopyable T>
UniqueObjectPoolPtr<Relation<T>> RelationPool<T>::get_or_allocate(ColumnsView columns)
{
    auto relation = m_pools[columns.size()].get_or_allocate(columns);
    if (relation->m_index == std::numeric_limits<size_t>::max())
        relation->m_index = m_factory.next_index();
    return relation;
}

template<TriviallyCopyable T>
UniqueObjectPoolPtr<Relation<T>> RelationPool<T>::get_or_allocate(std::span<const Column> columns)
{
    return get_or_allocate(ColumnsView(columns));
}

template<TriviallyCopyable T>
UniqueObjectPoolPtr<Relation<T>> RelationPool<T>::get_or_allocate(std::initializer_list<Column> columns)
{
    return get_or_allocate(std::span<const Column>(columns));
}

}  // namespace ygg::database

#endif
