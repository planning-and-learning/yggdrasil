/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_DETAILS_JOIN_INDEX_HPP_
#define YGG_DATABASE_DETAILS_JOIN_INDEX_HPP_

#include "yggdrasil/database/join_index.hpp"
#include "yggdrasil/semantics/hash.hpp"

#include <algorithm>
#include <ranges>
#include <stdexcept>

namespace ygg::database
{

namespace detail
{
template<TriviallyCopyable T>
auto join_key_values(std::span<const T> tuple, std::span<const size_t> positions)
{
    return positions | std::views::transform([tuple](size_t position) -> const T& { return tuple[position]; });
}

template<RelationViewConcept V>
void build_join_index(const V& build, std::span<const size_t> keys, UnorderedMultiMap<hash_t, size_t>& index)
{
    index.clear();
    index.reserve(build.size());
    for (size_t i = 0; i < build.size(); ++i)
        index.insert(ygg::hash_range(join_key_values(build.row(i), keys)), i);
}
}  // namespace detail

template<TriviallyCopyable T>
template<RelationViewConcept<T> V>
JoinIndex<T>::JoinIndex(const V& build, std::span<const size_t> key_positions) :
    m_relation_index(build.get_storage_index()),
    m_keys(key_positions.begin(), key_positions.end())
{
    if (m_relation_index == std::numeric_limits<size_t>::max())
        throw std::invalid_argument("JoinIndex: use a RelationPool to create indexed inputs.");
    for (const auto position : key_positions)
        if (position >= build.arity())
            throw std::out_of_range("JoinIndex: key position is outside the relation schema.");
    if (!key_positions.empty())
        detail::build_join_index(build, key_positions, m_index);
}

template<TriviallyCopyable T>
template<RelationViewConcept<T> V>
bool JoinIndex<T>::matches(const V& relation, std::span<const size_t> key_positions) const noexcept
{
    return ygg::EqualTo<JoinIndex<T>> {}(*this, std::make_tuple(relation.get_storage_index(), key_positions));
}

template<TriviallyCopyable T>
template<RelationViewConcept<T> V>
const JoinIndex<T>& JoinIndexCache<T>::get_or_create(const V& relation, std::span<const size_t> key_positions)
{
    // Canonical empty row sets can be shared across different schema arities.
    // Validate the current schema even when the row-storage key is cached.
    for (const auto position : key_positions)
        if (position >= relation.arity())
            throw std::out_of_range("JoinIndex: key position is outside the relation schema.");
    const auto found = m_indexes.find(std::make_tuple(relation.get_storage_index(), key_positions));
    if (found != m_indexes.end())
        return *found;
    return *m_indexes.emplace(relation, key_positions).first;
}

}  // namespace ygg::database

#endif
