/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_INCREMENTAL_JOIN_HPP_
#define YGG_DATABASE_SEMANTICS_INCREMENTAL_JOIN_HPP_

#include "yggdrasil/containers/unordered_multi_map.hpp"
#include "yggdrasil/database/semantics/incremental/delta.hpp"
#include "yggdrasil/database/semantics/operations.hpp"

#include <cassert>
#include <utility>

namespace ygg::database::incremental
{

/// First-order join delta rules as in Gupta et al. (SIGMOD 1993), Section 4:
/// https://doi.org/10.1145/170035.170066. See also Budiu et al., "DBSP"
/// (PVLDB 2023), Theorem 3.4: https://doi.org/10.14778/3587136.3587137.
/// Maintains natural join when either or both inputs change. Owns the current
/// input sets and their mutable indexes; supplied views are borrowed only during
/// calls. Inputs must not alias this evaluator's result/delta or the sequential
/// caller workspace. Results and deltas are borrowed until the next mutation.
/// Rejected schema/overlap checks preserve the previous evaluation. After a failure
/// during mutation, results and deltas are unavailable until initialize().
/// No repository publication is required.
template<ColumnTypes Values = DefaultColumnTypes>
class JoinEvaluator
{
    JoinPlan<Values> m_plan;
    Builder<Relation<Values>> m_lhs;
    Builder<Relation<Values>> m_rhs;
    UnorderedMultiMap<hash_t, uint_t> m_lhs_index;
    UnorderedMultiMap<hash_t, uint_t> m_rhs_index;
    Builder<Relation<Values>> m_result;
    Delta<Values> m_delta;
    bool m_initialized = false;

    template<RelationViewConcept<Values> V>
    static void insert_rows(const V& input, Builder<Relation<Values>>& rows, std::span<const ColumnSlice> keys, UnorderedMultiMap<hash_t, uint_t>& index);

    template<RelationViewConcept<Values> V>
    static void erase_rows(const V& input, Builder<Relation<Values>>& rows, std::span<const ColumnSlice> keys, UnorderedMultiMap<hash_t, uint_t>& index);

    template<RelationViewConcept<Values> L, RelationViewConcept<Values> R>
    void join(const L& lhs, const R& rhs, bool build_left, Builder<Relation<Values>>& output, Workspace<Values>& workspace) const;

    /// Applies changes that are known to be valid.
    template<RelationChange<Values> L, RelationChange<Values> R>
    void apply(const L& lhs, const R& rhs, Workspace<Values>& workspace);

    friend class QueryEvaluator<Values>;

public:
    explicit JoinEvaluator(JoinPlan<Values> plan);

    /// Replace the baseline, retaining buffers, and clear the last output delta.
    template<RelationViewConcept<Values> L, RelationViewConcept<Values> R>
    void initialize(const L& lhs, const R& rhs, Workspace<Values>& workspace);

    /// Each change describes actual set changes in that input's original column
    /// order. Pass empty changes for an unchanged side; swap added/removed on
    /// both sides to undo. Reports the exact net change of the complete batch.
    template<RelationChange<Values> L, RelationChange<Values> R>
    void update(const L& lhs, const R& rhs, Workspace<Values>& workspace);

    const Builder<Relation<Values>>& get_result() const&;
    const Builder<Relation<Values>>& get_result() const&& = delete;
    const Delta<Values>& get_delta() const&;
    const Delta<Values>& get_delta() const&& = delete;

    /// Retained inputs, result, delta and indexes; excludes plan and caller workspace.
    size_t memory_usage() const noexcept;
};

template<ColumnTypes Values>
template<RelationViewConcept<Values> V>
void JoinEvaluator<Values>::insert_rows(const V& input,
                                        Builder<Relation<Values>>& rows,
                                        std::span<const ColumnSlice> keys,
                                        UnorderedMultiMap<hash_t, uint_t>& index)
{
    if (!keys.empty())
        index.reserve_values(rows.size() + input.size());
    for (size_t i = 0; i < input.size(); ++i)
    {
        const auto row = input.row(i);
        const auto previous_size = rows.size();
        const auto position = rows.insert(Row<Values>(row, input.columns().span()));
        if (position != previous_size)
            throw std::invalid_argument("Incremental join: added row is already present in the input.");
        if (!keys.empty())
            index.insert(database::detail::join_key_hash(row, keys), position);
    }
}

template<ColumnTypes Values>
template<RelationViewConcept<Values> V>
void JoinEvaluator<Values>::erase_rows(const V& input,
                                       Builder<Relation<Values>>& rows,
                                       std::span<const ColumnSlice> keys,
                                       UnorderedMultiMap<hash_t, uint_t>& index)
{
    for (size_t i = 0; i < input.size(); ++i)
    {
        const auto position = rows.find(Row<Values>(input.row(i), input.columns().span()));
        if (!position)
            throw std::invalid_argument("Incremental join: removed row is absent from the input.");
        const auto last = ygg::to_uint_t(rows.size() - 1);
        if (keys.empty())
        {
            rows.erase(*position);
            continue;
        }
        const auto removed_key = database::detail::join_key_hash(rows.row(*position), keys);
        rows.erase(*position);
        [[maybe_unused]] const auto erased = index.erase(removed_key, *position);
        assert(erased);
        if (*position != last)
        {
            const auto moved_key = database::detail::join_key_hash(rows.row(*position), keys);
            [[maybe_unused]] const auto replaced = index.replace(moved_key, last, *position);
            assert(replaced);
        }
    }
}

template<ColumnTypes Values>
template<RelationViewConcept<Values> L, RelationViewConcept<Values> R>
void JoinEvaluator<Values>::join(const L& lhs, const R& rhs, bool build_left, Builder<Relation<Values>>& output, Workspace<Values>& workspace) const
{
    database::detail::join_rows<Values>(lhs,
                                        rhs,
                                        m_plan.output_columns().span(),
                                        m_plan.lhs_keys(),
                                        m_plan.rhs_keys(),
                                        m_plan.rhs_payload(),
                                        build_left,
                                        build_left ? m_lhs_index : m_rhs_index,
                                        output,
                                        workspace);
}

template<ColumnTypes Values>
JoinEvaluator<Values>::JoinEvaluator(JoinPlan<Values> plan) :
    m_plan(std::move(plan)),
    m_lhs(m_plan.lhs_columns().span()),
    m_rhs(m_plan.rhs_columns().span()),
    m_result(m_plan.output_columns().span()),
    m_delta(m_plan.output_columns().span())
{
}

template<ColumnTypes Values>
template<RelationViewConcept<Values> L, RelationViewConcept<Values> R>
void JoinEvaluator<Values>::initialize(const L& lhs, const R& rhs, Workspace<Values>& workspace)
{
    database::detail::require_plan_columns(lhs.columns().span(), m_plan.lhs_columns().span());
    database::detail::require_plan_columns(rhs.columns().span(), m_plan.rhs_columns().span());
    detail::require_unaliased(lhs, m_result, m_delta);
    detail::require_unaliased(rhs, m_result, m_delta);
    m_initialized = false;
    m_lhs.clear();
    m_rhs.clear();
    m_lhs_index.clear();
    m_rhs_index.clear();
    m_result.clear();
    m_delta.clear();
    insert_rows(lhs, m_lhs, m_plan.lhs_keys(), m_lhs_index);
    insert_rows(rhs, m_rhs, m_plan.rhs_keys(), m_rhs_index);
    // Both indexes are ready, so probe the smaller input.
    join(m_lhs, m_rhs, m_lhs.size() >= m_rhs.size(), m_result, workspace);
    m_initialized = true;
}

template<ColumnTypes Values>
template<RelationChange<Values> L, RelationChange<Values> R>
void JoinEvaluator<Values>::update(const L& lhs, const R& rhs, Workspace<Values>& workspace)
{
    detail::require_initialized(m_initialized);
    detail::require_change(lhs, m_plan.lhs_columns().span(), m_result, m_delta);
    detail::require_change(rhs, m_plan.rhs_columns().span(), m_result, m_delta);
    apply(lhs, rhs, workspace);
}

template<ColumnTypes Values>
template<RelationChange<Values> L, RelationChange<Values> R>
void JoinEvaluator<Values>::apply(const L& lhs, const R& rhs, Workspace<Values>& workspace)
{
    const auto& [lhs_added, lhs_removed] = lhs;
    const auto& [rhs_added, rhs_removed] = rhs;
    m_initialized = false;
    m_delta.clear();

    // Remove from both sides before inserting. Each old/new pair is visited
    // once, and no temporary match between an added and removed row is emitted.
    join(lhs_removed, m_rhs, false, m_delta.removed, workspace);
    erase_rows(lhs_removed, m_lhs, m_plan.lhs_keys(), m_lhs_index);
    join(m_lhs, rhs_removed, true, m_delta.removed, workspace);
    erase_rows(rhs_removed, m_rhs, m_plan.rhs_keys(), m_rhs_index);
    join(lhs_added, m_rhs, false, m_delta.added, workspace);
    insert_rows(lhs_added, m_lhs, m_plan.lhs_keys(), m_lhs_index);
    join(m_lhs, rhs_added, true, m_delta.added, workspace);
    insert_rows(rhs_added, m_rhs, m_plan.rhs_keys(), m_rhs_index);

    detail::apply_delta(m_delta, m_result);
    m_initialized = true;
}

template<ColumnTypes Values>
const Builder<Relation<Values>>& JoinEvaluator<Values>::get_result() const&
{
    detail::require_initialized(m_initialized);
    return m_result;
}

template<ColumnTypes Values>
const Delta<Values>& JoinEvaluator<Values>::get_delta() const&
{
    detail::require_initialized(m_initialized);
    return m_delta;
}

template<ColumnTypes Values>
size_t JoinEvaluator<Values>::memory_usage() const noexcept
{
    return m_lhs.memory_usage() + m_rhs.memory_usage() + m_result.memory_usage() + m_delta.memory_usage() + m_lhs_index.memory_usage()
           + m_rhs_index.memory_usage();
}

}  // namespace ygg::database::incremental

#endif
