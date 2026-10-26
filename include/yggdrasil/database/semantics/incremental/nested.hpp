/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_SEMANTICS_INCREMENTAL_NESTED_HPP_
#define YGG_DATABASE_SEMANTICS_INCREMENTAL_NESTED_HPP_

#include "yggdrasil/database/semantics/incremental/trace.hpp"
#include "yggdrasil/database/semantics/plans.hpp"
#include "yggdrasil/database/semantics/zset.hpp"

#include <map>
#include <vector>

/// The incremental versions of operators on nested streams [Budiu23, §6]: inside a fixpoint, a stream's value depends on
/// the update and the iteration, and operators exchange its changes over both, the change at iteration i of the current
/// update. Each operator keeps the traces it reads and is driven once per iteration by step(); commit() ends an update.
namespace ygg::database::incremental
{
/// Natural join of two nested streams (bilinear, Thm. 3.4 over nested time): a change at iteration i joins the other
/// input's history, and each product counts at the later of its two iterations; products for a later iteration are kept
/// until it is reached.
class NestedJoin
{
    JoinPlan m_plan;
    Trace m_lhs;
    Trace m_rhs;
    std::map<Iteration, ZSet> m_future;
    std::vector<std::byte> m_row;

    void emit(std::span<const std::byte> lhs, std::span<const std::byte> rhs, Weight weight, Iteration at, Iteration now, ZSet& output);

public:
    explicit NestedJoin(JoinPlan plan);

    [[nodiscard]] std::span<const ColumnLayout> columns() const noexcept { return m_plan.output_columns().span(); }
    /// The output's change at `iteration` from the inputs' changes at it.
    void step(const ZSet& lhs, const ZSet& rhs, Iteration iteration, ZSet& output);
    /// Whether products for an iteration after `iteration` are pending.
    [[nodiscard]] bool pending_after(Iteration iteration) const noexcept;
    void commit();
    void clear() noexcept;
    [[nodiscard]] size_t memory_usage() const noexcept;
};

/// distinct over a nested stream: the change of distinct(V) at (update t, iteration i) from the values of V at the four
/// corners (t or t − 1, i or i − 1), the history kept in a trace [Budiu23, §6; Feldera's DistinctIncremental]. A row
/// changed at iteration i is evaluated again at each later iteration of its history.
class NestedDistinct
{
    Trace m_input;
    std::map<Iteration, Builder<Relation>> m_scheduled;
    Builder<Relation> m_candidates;

public:
    explicit NestedDistinct(std::span<const ColumnLayout> columns);

    void step(const ZSet& input, Iteration iteration, ZSet& output);
    [[nodiscard]] bool pending_after(Iteration iteration) const noexcept;
    void commit();
    void clear() noexcept;
    [[nodiscard]] size_t memory_usage() const noexcept;
};
}  // namespace ygg::database::incremental

#endif
