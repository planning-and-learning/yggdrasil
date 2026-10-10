/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_JOIN_INDEX_HPP_
#define YGG_DATABASE_SEMANTICS_JOIN_INDEX_HPP_

#include "yggdrasil/containers/unordered_multi_map.hpp"
#include "yggdrasil/containers/unordered_set.hpp"
#include "yggdrasil/database/semantics/plans.hpp"
#include "yggdrasil/database/semantics/relation_view.hpp"

#include <cstddef>
#include <span>
#include <tuple>
#include <vector>

namespace ygg::database
{

/// Reusable hash index for one immutable relation and ordered field slices.
/// Indexed inputs must share one RelationPoolFactory; directly constructed inputs
/// may be used as unindexed probes. Rows stay unchanged until the index is destroyed.
/// Stores their factory-local identity; no row pointers or column labels are retained.
template<ColumnTypes Values = DefaultColumnTypes>
class JoinIndex
{
private:
    size_t m_relation_index;
    std::vector<ColumnSlice> m_keys;
    UnorderedMultiMap<hash_t, size_t> m_index;

public:
    template<RelationViewConcept<Values> V>
    JoinIndex(const V& build, std::span<const ColumnSlice> keys);

    template<RelationViewConcept<Values> V>
    bool matches(const V& relation, std::span<const ColumnSlice> keys) const noexcept;
    const UnorderedMultiMap<hash_t, size_t>& index() const noexcept { return m_index; }
    auto identifying_members() const noexcept { return std::make_tuple(m_relation_index, std::span<const ColumnSlice>(m_keys)); }
};

/// Shares indexes across renamed views and plans using the same ordered key slices.
/// All indexed inputs must come from the same RelationPoolFactory.
/// Indexed rows must remain stationary and unchanged. Clear before any source
/// is mutated, destroyed, moved, or reused. No schemas or views are retained.
template<ColumnTypes Values = DefaultColumnTypes>
class JoinIndexCache
{
private:
    UnorderedSet<JoinIndex<Values>> m_indexes;

public:
    /// Hits borrow the lookup key and allocate nothing. Returned references
    /// remain valid until an insertion or clear().
    template<RelationViewConcept<Values> V>
    const JoinIndex<Values>& get_or_create(const V& relation, std::span<const ColumnSlice> keys);

    size_t size() const noexcept { return m_indexes.size(); }
    void clear() noexcept { m_indexes.clear(); }
};

}  // namespace ygg::database

#include "yggdrasil/database/semantics/details/join_index.hpp"

#endif
