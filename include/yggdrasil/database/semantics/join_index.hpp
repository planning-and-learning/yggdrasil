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
#include "yggdrasil/semantics/hash.hpp"

#include <algorithm>
#include <boost/hash2/xxhash.hpp>
#include <cstddef>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace ygg::database
{

/// Reusable hash index for one immutable relation and ordered field slices.
/// Indexed inputs must share one RelationPoolFactory; directly constructed inputs
/// may be used as unindexed probes. Rows stay unchanged until the index is destroyed.
/// Stores their factory-local identity; no row pointers or column labels are retained.
class JoinIndex
{
private:
    size_t m_relation_index;
    std::vector<ColumnSlice> m_keys;
    UnorderedMultiMap<hash_t, size_t> m_index;

public:
    template<RelationViewConcept V>
    JoinIndex(const V& build, std::span<const ColumnSlice> keys);

    template<RelationViewConcept V>
    bool matches(const V& relation, std::span<const ColumnSlice> keys) const noexcept;
    [[nodiscard]] const UnorderedMultiMap<hash_t, size_t>& index() const noexcept { return m_index; }

    [[nodiscard]] auto identifying_members() const noexcept { return std::make_tuple(m_relation_index, std::span<const ColumnSlice>(m_keys)); }
};

/// Shares indexes across renamed views and plans using the same ordered key slices.
/// All indexed inputs must come from the same RelationPoolFactory.
/// Indexed rows must remain stationary and unchanged. Clear before any source
/// is mutated, destroyed, moved, or reused. No schemas or views are retained.
class JoinIndexCache
{
private:
    UnorderedSet<JoinIndex> m_indexes;

public:
    /// Hits borrow the lookup key and allocate nothing. Returned references
    /// remain valid until an insertion or clear().
    template<RelationViewConcept V>
    const JoinIndex& get_or_create(const V& relation, std::span<const ColumnSlice> keys);

    [[nodiscard]] size_t size() const noexcept { return m_indexes.size(); }
    void clear() noexcept { m_indexes.clear(); }
};

}  // namespace ygg::database

namespace ygg::database
{

namespace detail
{
/// Hash canonical field bytes in logical key order, without materializing a key.
hash_t join_key_hash(std::span<const std::byte> row, std::span<const ColumnSlice> keys) noexcept;

bool join_keys_equal(std::span<const std::byte> lhs,
                     std::span<const ColumnSlice> lhs_keys,
                     std::span<const std::byte> rhs,
                     std::span<const ColumnSlice> rhs_keys) noexcept;

void require_key_slices(std::span<const ColumnLayout> columns, std::span<const ColumnSlice> keys);

template<RelationViewConcept V>
void build_join_index(const V& build, std::span<const ColumnSlice> keys, UnorderedMultiMap<hash_t, size_t>& index)
{
    index.clear();
    index.reserve(build.size());
    for (size_t i = 0; i < build.size(); ++i)
        index.insert(join_key_hash(build.row(i), keys), i);
}
}  // namespace detail

template<RelationViewConcept V>
JoinIndex::JoinIndex(const V& build, std::span<const ColumnSlice> keys) : m_relation_index(build.get_storage_index()), m_keys(keys.begin(), keys.end())
{
    if (m_relation_index == std::numeric_limits<size_t>::max())
        throw std::invalid_argument("JoinIndex: use a RelationPool to create indexed inputs.");
    detail::require_key_slices(build.columns().span(), keys);
    if (!keys.empty())
        detail::build_join_index(build, keys, m_index);
}

template<RelationViewConcept V>
bool JoinIndex::matches(const V& relation, std::span<const ColumnSlice> keys) const noexcept
{ return ygg::EqualTo<JoinIndex> {}(*this, std::make_tuple(relation.get_storage_index(), keys)); }

template<RelationViewConcept V>
const JoinIndex& JoinIndexCache::get_or_create(const V& relation, std::span<const ColumnSlice> keys)
{
    // Canonical empty row sets can be shared across different schema arities.
    // Validate the current schema even when the row-storage key is cached.
    detail::require_key_slices(relation.columns().span(), keys);
    const auto found = m_indexes.find(std::make_tuple(relation.get_storage_index(), keys));
    if (found != m_indexes.end())
        return *found;
    return *m_indexes.emplace(relation, keys).first;
}

}  // namespace ygg::database

#endif
