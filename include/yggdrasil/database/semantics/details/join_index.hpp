/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_DETAILS_JOIN_INDEX_HPP_
#define YGG_DATABASE_SEMANTICS_DETAILS_JOIN_INDEX_HPP_

#include "yggdrasil/database/semantics/join_index.hpp"
#include "yggdrasil/semantics/hash.hpp"

#include <algorithm>
#include <boost/hash2/xxhash.hpp>
#include <limits>
#include <ranges>
#include <stdexcept>

namespace ygg::database
{

namespace detail
{
/// Hash canonical field bytes in logical key order, without materializing a key.
inline hash_t join_key_hash(std::span<const std::byte> row, std::span<const ColumnSlice> keys) noexcept
{
    auto hash = boost::hash2::xxhash_64 {};
    for (const auto key : keys)
        hash.update(row.data() + key.offset, key.size);
    return hash.result();
}

inline bool join_keys_equal(std::span<const std::byte> lhs,
                            std::span<const ColumnSlice> lhs_keys,
                            std::span<const std::byte> rhs,
                            std::span<const ColumnSlice> rhs_keys) noexcept
{
    for (size_t i = 0; i < lhs_keys.size(); ++i)
        if (!std::ranges::equal(lhs.subspan(lhs_keys[i].offset, lhs_keys[i].size), rhs.subspan(rhs_keys[i].offset, rhs_keys[i].size)))
            return false;
    return true;
}

inline void require_key_slices(std::span<const ColumnLayout> columns, std::span<const ColumnSlice> keys)
{
    for (const auto key : keys)
        if (std::ranges::none_of(columns, [&](const auto& column) { return column_slice(column) == key; }))
            throw std::out_of_range("JoinIndex: key does not identify a field in the relation schema.");
}

template<ColumnTypes Values, RelationViewConcept<Values> V>
void build_join_index(const V& build, std::span<const ColumnSlice> keys, UnorderedMultiMap<hash_t, size_t>& index)
{
    index.clear();
    index.reserve(build.size());
    for (size_t i = 0; i < build.size(); ++i)
        index.insert(join_key_hash(build.row(i), keys), i);
}
}  // namespace detail

template<ColumnTypes Values>
template<RelationViewConcept<Values> V>
JoinIndex<Values>::JoinIndex(const V& build, std::span<const ColumnSlice> keys) : m_relation_index(build.get_storage_index()), m_keys(keys.begin(), keys.end())
{
    if (m_relation_index == std::numeric_limits<size_t>::max())
        throw std::invalid_argument("JoinIndex: use a RelationPool to create indexed inputs.");
    detail::require_key_slices(build.columns().span(), keys);
    if (!keys.empty())
        detail::build_join_index<Values>(build, keys, m_index);
}

template<ColumnTypes Values>
template<RelationViewConcept<Values> V>
bool JoinIndex<Values>::matches(const V& relation, std::span<const ColumnSlice> keys) const noexcept
{
    return ygg::EqualTo<JoinIndex<Values>> {}(*this, std::make_tuple(relation.get_storage_index(), keys));
}

template<ColumnTypes Values>
template<RelationViewConcept<Values> V>
const JoinIndex<Values>& JoinIndexCache<Values>::get_or_create(const V& relation, std::span<const ColumnSlice> keys)
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
