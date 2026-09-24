/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_RELATION_POOL_HPP_
#define YGG_DATABASE_RELATION_POOL_HPP_

#include "yggdrasil/containers/unique_object_pool.hpp"
#include "yggdrasil/database/relation.hpp"

#include <map>
#include <memory>
#include <stdexcept>

namespace ygg::database
{

/// Creates pools sharing one sequence of row-storage identities.
/// Copies share the sequence. A JoinIndexCache must only use one factory's IDs.
/// Like the relations themselves, a factory is not thread-safe.
template<TriviallyCopyable T = uint_t>
class RelationPoolFactory
{
private:
    std::shared_ptr<size_t> m_next_index = std::make_shared<size_t>(0);

    friend class RelationPool<T>;

    size_t next_index()
    {
        if (*m_next_index == std::numeric_limits<size_t>::max())
            throw std::overflow_error("RelationPoolFactory: storage index space exhausted.");
        return (*m_next_index)++;
    }

public:
    [[nodiscard]] RelationPool<T> create_pool();
};

/// Reuses temporary relations by arity, retaining their tuple/hash buffers.
/// Handles return relations automatically. The pool must outlive every handle;
/// no view or row span may be used after its relation is returned to the pool.
/// Move handles to transfer ownership; pooled relations must retain their storage.
template<TriviallyCopyable T = uint_t>
class RelationPool
{
private:
    RelationPoolFactory<T> m_factory;
    // Node storage keeps each nonmovable object pool at a stable address.
    std::map<size_t, UniqueObjectPool<Relation<T>>> m_pools;

public:
    RelationPool() = default;
    explicit RelationPool(RelationPoolFactory<T> factory) : m_factory(std::move(factory)) {}
    RelationPool(const RelationPool&) = delete;
    RelationPool& operator=(const RelationPool&) = delete;

    [[nodiscard]] UniqueObjectPoolPtr<Relation<T>> get_or_allocate(ColumnsView columns);
    [[nodiscard]] UniqueObjectPoolPtr<Relation<T>> get_or_allocate(std::span<const Column> columns);
    [[nodiscard]] UniqueObjectPoolPtr<Relation<T>> get_or_allocate(std::initializer_list<Column> columns);
};

}  // namespace ygg::database

#include "yggdrasil/database/details/relation_pool.hpp"

#endif
