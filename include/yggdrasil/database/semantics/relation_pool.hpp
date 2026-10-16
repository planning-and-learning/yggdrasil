/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_RELATION_POOL_HPP_
#define YGG_DATABASE_SEMANTICS_RELATION_POOL_HPP_

#include "yggdrasil/containers/associative_containers.hpp"
#include "yggdrasil/containers/unique_object_pool.hpp"
#include "yggdrasil/database/semantics/relation_builder.hpp"

#include <memory>
#include <stdexcept>

namespace ygg::database
{

/// Creates pools sharing one sequence of row-storage identities.
/// Copies share the sequence. A JoinIndexCache must only use one factory's IDs.
/// Like the relations themselves, a factory is not thread-safe.
template<ColumnTypes Values>
class RelationPoolFactory
{
private:
    std::shared_ptr<size_t> m_next_index = std::make_shared<size_t>(0);

    friend class RelationPool<Values>;
    friend class RelationRepository<Values>;

    size_t next_index()
    {
        if (*m_next_index == std::numeric_limits<size_t>::max())
            throw std::overflow_error("RelationPoolFactory: storage index space exhausted.");
        return (*m_next_index)++;
    }

public:
    [[nodiscard]] RelationPool<Values> create_pool();
};

/// Reuses temporary relations by byte width, retaining their tuple/hash buffers.
/// Handles return relations automatically. The pool must outlive every handle;
/// no view or row span may be used after its relation is returned to the pool.
/// Move handles to transfer ownership; pooled relations must retain their storage.
template<ColumnTypes Values>
class RelationPool
{
private:
    RelationPoolFactory<Values> m_factory;
    /// By row width. Handles point back to their pool, which therefore lives at a stable address behind a pointer.
    UnorderedMap<size_t, std::unique_ptr<UniqueObjectPool<ygg::Builder<Relation<Values>>>>> m_pools;

    template<ColumnSchema Schema>
    UniqueObjectPoolPtr<ygg::Builder<Relation<Values>>> allocate(size_t width, Schema columns);

public:
    RelationPool() = default;
    explicit RelationPool(RelationPoolFactory<Values> factory) : m_factory(std::move(factory)) {}
    RelationPool(const RelationPool&) = delete;
    RelationPool& operator=(const RelationPool&) = delete;
    // Handles point to the object pools, which stay at their heap addresses when this moves.
    RelationPool(RelationPool&&) noexcept = default;
    RelationPool& operator=(RelationPool&&) noexcept = default;

    [[nodiscard]] UniqueObjectPoolPtr<ygg::Builder<Relation<Values>>> get_or_allocate(std::span<const ColumnLayout> columns);
    [[nodiscard]] UniqueObjectPoolPtr<ygg::Builder<Relation<Values>>> get_or_allocate(std::span<const Index<Column>> columns);
    [[nodiscard]] UniqueObjectPoolPtr<ygg::Builder<Relation<Values>>> get_or_allocate(std::initializer_list<Index<Column>> columns);
};

}  // namespace ygg::database

#include "yggdrasil/database/semantics/details/relation_pool.hpp"

#endif
