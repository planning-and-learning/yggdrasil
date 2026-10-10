/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_INCREMENTAL_DELTA_HPP_
#define YGG_DATABASE_SEMANTICS_INCREMENTAL_DELTA_HPP_

#include "yggdrasil/database/semantics/operations.hpp"

#include <cassert>
#include <initializer_list>
#include <ranges>
#include <tuple>
#include <utility>
#include <span>
#include <stdexcept>

namespace ygg::database::incremental
{
template<ColumnTypes Values>
class QueryEvaluator;

/// Actual set changes with one ordered schema. Added and removed rows are
/// disjoint. The caller guarantees additions were absent and removals present.
/// Exchanging the two inputs to update() reverses the change.
template<ColumnTypes Values = DefaultColumnTypes>
struct Delta
{
    Builder<Relation<Values>> added;
    Builder<Relation<Values>> removed;

    explicit Delta(std::span<const ColumnLayout> columns) : added(columns), removed(columns) {}
    explicit Delta(std::span<const Index<Column>> columns) : added(columns), removed(columns) {}
    Delta(std::initializer_list<Index<Column>> columns) : Delta(std::span<const Index<Column>>(columns.begin(), columns.size())) {}

    void clear() noexcept
    {
        added.clear();
        removed.clear();
    }
    /// The (added, removed) change.
    std::pair<const Builder<Relation<Values>>&, const Builder<Relation<Values>>&> change() const noexcept { return { added, removed }; }
    size_t memory_usage() const noexcept { return added.memory_usage() + removed.memory_usage(); }
};

/// An actual set change of one input: disjoint (added, removed) relations, read with std::get.
template<typename C, typename Values>
concept RelationChange = requires(const C& change) {
    requires RelationViewConcept<decltype(std::get<0>(change)), Values>;
    requires RelationViewConcept<decltype(std::get<1>(change)), Values>;
};

/// Sized random-access changes, one per input.
template<typename R, typename Values>
concept RelationDeltaRange = std::ranges::random_access_range<const R> && std::ranges::sized_range<const R>
                             && RelationChange<std::ranges::range_reference_t<const R>, Values>;

namespace detail
{
template<ColumnTypes Values, RelationViewConcept<Values> A, RelationViewConcept<Values> R>
void require_delta(const A& added, const R& removed, std::span<const ColumnLayout> columns)
{
    database::detail::require_plan_columns(added.columns().span(), columns);
    database::detail::require_plan_columns(removed.columns().span(), columns);
    for (size_t i = 0; i < added.size(); ++i)
        if (removed.contains(Row<Values>(added.row(i), added.columns().span())))
            throw std::invalid_argument("Incremental evaluation: added and removed rows overlap.");
}

/// Results and deltas exist only after a complete initialize or update.
inline void require_initialized(bool initialized)
{
    if (!initialized)
        throw std::logic_error("Incremental evaluation: initialize before use.");
}

/// Inputs are borrowed during a call and must not be the evaluator's own result or delta.
template<ColumnTypes Values, RelationViewConcept<Values> V>
void require_unaliased(const V& input, const Builder<Relation<Values>>& result, const Delta<Values>& delta)
{
    const auto* address = input.get_storage_address();
    if (address == result.get_storage_address() || address == delta.added.get_storage_address() || address == delta.removed.get_storage_address())
        throw std::invalid_argument("Incremental evaluation: input aliases the evaluator's result or delta.");
}

/// A change of an input with the given columns is disjoint and borrows no evaluator storage.
template<ColumnTypes Values, RelationChange<Values> C>
void require_change(const C& change, std::span<const ColumnLayout> columns, const Builder<Relation<Values>>& result, const Delta<Values>& delta)
{
    require_delta<Values>(std::get<0>(change), std::get<1>(change), columns);
    require_unaliased(std::get<0>(change), result, delta);
    require_unaliased(std::get<1>(change), result, delta);
}

/// Applies an exact delta of the result: removed rows are present, added rows absent.
template<ColumnTypes Values>
void apply_delta(const Delta<Values>& delta, Builder<Relation<Values>>& result)
{
    const auto columns = result.columns().span();
    for (size_t i = 0; i < delta.removed.size(); ++i)
    {
        const auto position = result.find(Row<Values>(delta.removed.row(i), columns));
        assert(position);
        result.erase(*position);
    }
    for (size_t i = 0; i < delta.added.size(); ++i)
        result.insert(Row<Values>(delta.added.row(i), columns));
}
}  // namespace detail

}  // namespace ygg::database::incremental

#endif
