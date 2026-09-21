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
UniqueObjectPoolPtr<Relation<T>> RelationPool<T>::get_or_allocate(ColumnsView columns)
{
    return m_pools[columns.size()].get_or_allocate(columns);
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
