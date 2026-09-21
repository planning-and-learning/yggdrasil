/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_RELATION_POOL_HPP_
#define YGG_DATABASE_RELATION_POOL_HPP_

#include "yggdrasil/containers/unique_object_pool.hpp"
#include "yggdrasil/database/relation.hpp"

#include <map>

namespace ygg::database
{

/// Reuses temporary relations by arity, retaining their tuple/hash buffers.
/// Handles return relations automatically. The pool must outlive every handle;
/// no view or row span may be used after its relation is returned to the pool.
/// Move handles to transfer ownership; pooled relations must retain their storage.
template<TriviallyCopyable T = uint_t>
class RelationPool
{
private:
    // Node storage keeps each nonmovable object pool at a stable address.
    std::map<size_t, UniqueObjectPool<Relation<T>>> m_pools;

public:
    RelationPool() = default;
    RelationPool(const RelationPool&) = delete;
    RelationPool& operator=(const RelationPool&) = delete;

    [[nodiscard]] UniqueObjectPoolPtr<Relation<T>> get_or_allocate(ColumnsView columns);
    [[nodiscard]] UniqueObjectPoolPtr<Relation<T>> get_or_allocate(std::span<const Column> columns);
    [[nodiscard]] UniqueObjectPoolPtr<Relation<T>> get_or_allocate(std::initializer_list<Column> columns);
};

}  // namespace ygg::database

#include "yggdrasil/database/details/relation_pool.hpp"

#endif
