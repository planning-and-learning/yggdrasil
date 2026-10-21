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
namespace detail
{
/// Inserts rows absent from `rows`, indexing their positions by the hash of `keys`.
template<RelationViewConcept V>
void insert_rows(const V& input, Builder<Relation>& rows, std::span<const ColumnSlice> keys, UnorderedMultiMap<hash_t, uint_t>& index)
{
    if (!keys.empty())
        index.reserve_values(rows.size() + input.size());
    for (size_t i = 0; i < input.size(); ++i)
    {
        const auto row = input.row(i);
        const auto previous_size = rows.size();
        const auto position = rows.insert(Row(row, input.columns().span()));
        if (position != previous_size)
            throw std::invalid_argument("Incremental evaluation: added row is already present in the input.");
        if (!keys.empty())
            index.insert(database::detail::join_key_hash(row, keys), position);
    }
}

/// Erases rows present in `rows`, keeping the position index of insert_rows() current.
template<RelationViewConcept V>
void erase_rows(const V& input, Builder<Relation>& rows, std::span<const ColumnSlice> keys, UnorderedMultiMap<hash_t, uint_t>& index)
{
    for (size_t i = 0; i < input.size(); ++i)
    {
        const auto position = rows.find(Row(input.row(i), input.columns().span()));
        if (!position)
            throw std::invalid_argument("Incremental evaluation: removed row is absent from the input.");
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

}  // namespace detail

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
class JoinEvaluator
{
    JoinPlan m_plan;
    Builder<Relation> m_lhs;
    Builder<Relation> m_rhs;
    UnorderedMultiMap<hash_t, uint_t> m_lhs_index;
    UnorderedMultiMap<hash_t, uint_t> m_rhs_index;
    Builder<Relation> m_result;
    Delta m_delta;
    bool m_initialized = false;

    template<RelationViewConcept L, RelationViewConcept R>
    void join(const L& lhs, const R& rhs, bool build_left, Builder<Relation>& output, Workspace& workspace) const;

    /// Applies changes that are known to be valid.
    template<RelationChange L, RelationChange R>
    void apply(const L& lhs, const R& rhs, Workspace& workspace);

    friend struct detail::NodeRules<QueryJoinTag>;

public:
    explicit JoinEvaluator(JoinPlan plan);

    /// Replace the baseline, retaining buffers, and clear the last output delta.
    template<RelationViewConcept L, RelationViewConcept R>
    void initialize(const L& lhs, const R& rhs, Workspace& workspace);

    /// Each change describes actual set changes in that input's original column
    /// order. Pass empty changes for an unchanged side; swap added/removed on
    /// both sides to undo. Reports the exact net change of the complete batch.
    template<RelationChange L, RelationChange R>
    void update(const L& lhs, const R& rhs, Workspace& workspace);

    const Builder<Relation>& get_result() const&;
    const Builder<Relation>& get_result() const&& = delete;
    const Delta& get_delta() const&;
    const Delta& get_delta() const&& = delete;

    /// Retained inputs, result, delta and indexes; excludes plan and caller workspace.
    size_t memory_usage() const noexcept;
};

template<RelationViewConcept L, RelationViewConcept R>
void JoinEvaluator::join(const L& lhs, const R& rhs, bool build_left, Builder<Relation>& output, Workspace& workspace) const
{
    database::detail::join_rows(lhs,
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

template<RelationViewConcept L, RelationViewConcept R>
void JoinEvaluator::initialize(const L& lhs, const R& rhs, Workspace& workspace)
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
    detail::insert_rows(lhs, m_lhs, m_plan.lhs_keys(), m_lhs_index);
    detail::insert_rows(rhs, m_rhs, m_plan.rhs_keys(), m_rhs_index);
    // Both indexes are ready, so probe the smaller input.
    join(m_lhs, m_rhs, m_lhs.size() >= m_rhs.size(), m_result, workspace);
    m_initialized = true;
}

template<RelationChange L, RelationChange R>
void JoinEvaluator::update(const L& lhs, const R& rhs, Workspace& workspace)
{
    detail::require_initialized(m_initialized);
    detail::require_change(lhs, m_plan.lhs_columns().span(), m_result, m_delta);
    detail::require_change(rhs, m_plan.rhs_columns().span(), m_result, m_delta);
    apply(lhs, rhs, workspace);
}

template<RelationChange L, RelationChange R>
void JoinEvaluator::apply(const L& lhs, const R& rhs, Workspace& workspace)
{
    const auto& [lhs_added, lhs_removed] = lhs;
    const auto& [rhs_added, rhs_removed] = rhs;
    m_initialized = false;
    m_delta.clear();

    // Remove from both sides before inserting. Each old/new pair is visited
    // once, and no temporary match between an added and removed row is emitted.
    join(lhs_removed, m_rhs, false, m_delta.removed, workspace);
    detail::erase_rows(lhs_removed, m_lhs, m_plan.lhs_keys(), m_lhs_index);
    join(m_lhs, rhs_removed, true, m_delta.removed, workspace);
    detail::erase_rows(rhs_removed, m_rhs, m_plan.rhs_keys(), m_rhs_index);
    join(lhs_added, m_rhs, false, m_delta.added, workspace);
    detail::insert_rows(lhs_added, m_lhs, m_plan.lhs_keys(), m_lhs_index);
    join(m_lhs, rhs_added, true, m_delta.added, workspace);
    detail::insert_rows(rhs_added, m_rhs, m_plan.rhs_keys(), m_rhs_index);

    detail::apply_delta(m_delta, m_result);
    m_initialized = true;
}

}  // namespace ygg::database::incremental

#endif
