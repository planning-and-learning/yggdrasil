/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_INCREMENTAL_DELTA_HPP_
#define YGG_DATABASE_INCREMENTAL_DELTA_HPP_

#include "yggdrasil/database/operations.hpp"

#include <initializer_list>
#include <span>
#include <stdexcept>

namespace ygg::database::incremental
{

/// Actual set changes with one ordered schema. Added and removed rows are
/// disjoint. The caller guarantees additions were absent and removals present.
/// Exchanging the two inputs to update() reverses the change.
template<TriviallyCopyable T = uint_t>
struct Delta
{
    Builder<Relation<T>> added;
    Builder<Relation<T>> removed;

    explicit Delta(std::span<const Index<Column>> columns) : added(columns), removed(columns) {}
    Delta(std::initializer_list<Index<Column>> columns) : Delta(std::span<const Index<Column>>(columns.begin(), columns.size())) {}

    void clear() noexcept
    {
        added.clear();
        removed.clear();
    }
    size_t memory_usage() const noexcept { return added.memory_usage() + removed.memory_usage(); }
};

namespace detail
{
template<TriviallyCopyable T, RelationViewConcept<T> A, RelationViewConcept<T> R>
void require_delta(const A& added, const R& removed, std::span<const Index<Column>> columns)
{
    database::detail::require_plan_columns(added.columns().span(), columns);
    database::detail::require_plan_columns(removed.columns().span(), columns);
    for (size_t i = 0; i < added.size(); ++i)
        if (removed.contains(added.row(i)))
            throw std::invalid_argument("Incremental evaluation: added and removed rows overlap.");
}
}  // namespace detail

}  // namespace ygg::database::incremental

#endif
