/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_OPERATORS_INCREMENTAL_HPP_
#define YGG_DATABASE_OPERATORS_INCREMENTAL_HPP_

#include "yggdrasil/database/relation/columns.hpp"
#include "yggdrasil/database/relation/delta.hpp"
#include "yggdrasil/database/relation/relation_builder.hpp"
#include "yggdrasil/database/relation/relation_view.hpp"

#include <span>
#include <stdexcept>
#include <tuple>

/// What incremental operators share: the checks of their inputs and the application of their result's changes.

namespace ygg::database::incremental::detail
{
template<typename Tag>
struct NodeRules;

template<RelationViewConcept A, RelationViewConcept R>
void require_delta(const A& added, const R& removed, std::span<const ColumnLayout> columns)
{
    database::detail::require_plan_columns(added.columns().span(), columns);
    database::detail::require_plan_columns(removed.columns().span(), columns);
    for (size_t i = 0; i < added.size(); ++i)
        if (removed.contains(Row(added.row(i), added.columns().span())))
            throw std::invalid_argument("Incremental evaluation: added and removed rows overlap.");
}

/// Results and deltas exist only after a complete initialize or update.
void require_initialized(bool initialized);

/// Inputs are borrowed during a call and must not be the evaluator's own result or delta.
template<RelationViewConcept V>
void require_unaliased(const V& input, const Builder<Relation>& result, const Delta& delta)
{
    const auto* address = input.get_storage_address();
    if (address == result.get_storage_address() || address == delta.added.get_storage_address() || address == delta.removed.get_storage_address())
        throw std::invalid_argument("Incremental evaluation: input aliases the evaluator's result or delta.");
}

/// A change of an input with the given columns is disjoint and borrows no evaluator storage.
template<RelationChange C>
void require_change(const C& change, std::span<const ColumnLayout> columns, const Builder<Relation>& result, const Delta& delta)
{
    require_delta(std::get<0>(change), std::get<1>(change), columns);
    require_unaliased(std::get<0>(change), result, delta);
    require_unaliased(std::get<1>(change), result, delta);
}

/// Applies an exact delta of the result: removed rows are present, added rows absent.
void apply_delta(const Delta& delta, Builder<Relation>& result);
}  // namespace ygg::database::incremental::detail

#endif
