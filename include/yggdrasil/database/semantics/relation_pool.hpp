/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_RELATION_POOL_HPP_
#define YGG_DATABASE_SEMANTICS_RELATION_POOL_HPP_

#include <urd/repr/slots/slots.hpp>
#include <urd/store.hpp>
#include "yggdrasil/core/index_sequence.hpp"
#include "yggdrasil/database/semantics/relation_builder.hpp"

#include <memory>
#include <stdexcept>
#include <urd/repr/maps/std_map.hpp>
#include <urd/repr/maps/unordered_flat_map.hpp>
#include <urd/repr/values/unordered_flat_set.hpp>
#include <urd/store.hpp>

namespace ygg::database
{

/// Creates pools sharing one sequence of row-storage identities.
/// Copies share the sequence. A JoinIndexCache must only use one factory's IDs.
/// Like the relations themselves, a factory is not thread-safe.
class RelationPoolFactory
{
private:
    IndexSequence m_indices;

    friend class RelationPool;
    friend class RelationRepository;
};

/// Reuses temporary relations by byte width, retaining their tuple/hash buffers.
/// Handles return relations automatically. The pool must outlive every handle;
/// no view or row span may be used after its relation is returned to the pool.
/// Move handles to transfer ownership; pooled relations must retain their storage.
class RelationPool
{
private:
    RelationPoolFactory m_factory;
    /// By row width. Handles point back to their pool, which therefore lives at a stable address behind a pointer.
    urd::Store<urd::map<size_t, std::unique_ptr<urd::Store<urd::slots<urd::builder<Relation>>>>>> m_pools;

    template<ColumnSchema Schema>
    urd::unique_slot_ptr<urd::builder<Relation>> allocate(size_t width, Schema columns);

public:
    RelationPool() = default;
    explicit RelationPool(RelationPoolFactory factory) : m_factory(std::move(factory)) {}
    RelationPool(const RelationPool&) = delete;
    RelationPool& operator=(const RelationPool&) = delete;
    // Handles point to the object pools, which stay at their heap addresses when this moves.
    RelationPool(RelationPool&&) noexcept = default;
    RelationPool& operator=(RelationPool&&) noexcept = default;
    ~RelationPool() = default;

    [[nodiscard]] urd::unique_slot_ptr<urd::builder<Relation>> get_or_allocate(std::span<const ColumnLayout> columns);
    /// Labels alone give every column type T.
    template<ByteCodable T = uint_t>
    [[nodiscard]] urd::unique_slot_ptr<urd::builder<Relation>> get_or_allocate(std::span<const urd::index<Column>> columns);
    template<ByteCodable T = uint_t>
    [[nodiscard]] urd::unique_slot_ptr<urd::builder<Relation>> get_or_allocate(std::initializer_list<urd::index<Column>> columns);
};

}  // namespace ygg::database

namespace ygg::database
{

template<ColumnSchema Schema>
urd::unique_slot_ptr<urd::builder<Relation>> RelationPool::allocate(size_t width, Schema columns)
{
    auto& pool = m_pools[width];
    if (!pool)
        pool = std::make_unique<urd::Store<urd::slots<urd::builder<Relation>>>>();
    auto relation = pool->acquire_unique();
    relation->initialize(columns);
    if (relation->m_storage_index == std::numeric_limits<size_t>::max())
        relation->m_storage_index = m_factory.m_indices.next();
    return relation;
}

template<ByteCodable T>
urd::unique_slot_ptr<urd::builder<Relation>> RelationPool::get_or_allocate(std::span<const urd::index<Column>> columns)
{
    auto typed = urd::builder<Columns> {};
    typed.template assign<T>(columns);
    return allocate(typed.row_size(), typed.span());
}

template<ByteCodable T>
urd::unique_slot_ptr<urd::builder<Relation>> RelationPool::get_or_allocate(std::initializer_list<urd::index<Column>> columns)
{ return get_or_allocate<T>(std::span<const urd::index<Column>>(columns)); }

}  // namespace ygg::database

#endif
