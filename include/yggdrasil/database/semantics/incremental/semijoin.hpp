/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_INCREMENTAL_SEMIJOIN_HPP_
#define YGG_DATABASE_SEMANTICS_INCREMENTAL_SEMIJOIN_HPP_

#include "yggdrasil/containers/unordered_multi_map.hpp"
#include "yggdrasil/database/semantics/incremental/delta.hpp"
#include "yggdrasil/database/semantics/incremental/join.hpp"
#include "yggdrasil/database/semantics/operations.hpp"

#include <span>
#include <utility>

namespace ygg::database::incremental
{

/// Maintains the semijoin L ⋉ R, or the antijoin L ▷ R, when either or both inputs change. A row of L is in the
/// output iff it has a match in R on the shared columns (semijoin) or none (antijoin), so the rows whose membership
/// can change are the changed rows of L and the rows of L sharing a key with a changed row of R (the delta rules of
/// Gupta & Mumick, "Maintenance of Materialized Views", IEEE Data Eng. Bull. 1995, with an existence test in place
/// of the join). Owns the current inputs and their key indexes; supplied views are borrowed only during calls.
/// After a failure during mutation, results and deltas are unavailable until initialize().
class SemijoinEvaluator
{
    JoinPlan m_plan;
    bool m_anti;
    Builder<Relation> m_lhs;
    Builder<Relation> m_rhs;
    UnorderedMultiMap<hash_t, uint_t> m_lhs_index;
    UnorderedMultiMap<hash_t, uint_t> m_rhs_index;
    Builder<Relation> m_result;
    Delta m_delta;
    bool m_initialized = false;

    bool matched(std::span<const std::byte> lhs_row) const;
    bool kept(std::span<const std::byte> lhs_row) const { return matched(lhs_row) != m_anti; }

    /// Records the change of a row of the current L against the result before this update.
    void reconcile(std::span<const std::byte> lhs_row)
    {
        const auto row = Row(lhs_row, m_lhs.columns().span());
        const auto was = m_result.contains(row);
        const auto is = kept(lhs_row);
        if (is && !was)
            m_delta.added.insert(row);
        else if (!is && was)
            m_delta.removed.insert(row);
    }
    /// Reconciles the rows of the current L that share the key of a changed row of R.
    template<RelationViewConcept V>
    void reconcile_matches(const V& rhs_rows)
    {
        const auto lhs_keys = m_plan.lhs_keys();
        const auto rhs_keys = m_plan.rhs_keys();
        if (rhs_rows.empty())
            return;
        if (lhs_keys.empty())
        {
            for (size_t i = 0; i < m_lhs.size(); ++i)
                reconcile(m_lhs.row(i));
            return;
        }
        for (size_t i = 0; i < rhs_rows.size(); ++i)
        {
            const auto rhs_row = rhs_rows.row(i);
            for (const auto position : m_lhs_index.values(database::detail::join_key_hash(rhs_row, rhs_keys)))
                if (database::detail::join_keys_equal(m_lhs.row(position), lhs_keys, rhs_row, rhs_keys))
                    reconcile(m_lhs.row(position));
        }
    }

    template<RelationChange L, RelationChange R>
    void apply(const L& lhs, const R& rhs)
    {
        const auto& [lhs_added, lhs_removed] = lhs;
        const auto& [rhs_added, rhs_removed] = rhs;
        m_initialized = false;
        m_delta.clear();
        for (size_t i = 0; i < lhs_removed.size(); ++i)
            if (const auto row = Row(lhs_removed.row(i), m_lhs.columns().span()); m_result.contains(row))
                m_delta.removed.insert(row);
        detail::erase_rows(lhs_removed, m_lhs, m_plan.lhs_keys(), m_lhs_index);
        detail::erase_rows(rhs_removed, m_rhs, m_plan.rhs_keys(), m_rhs_index);
        detail::insert_rows(rhs_added, m_rhs, m_plan.rhs_keys(), m_rhs_index);
        detail::insert_rows(lhs_added, m_lhs, m_plan.lhs_keys(), m_lhs_index);
        for (size_t i = 0; i < lhs_added.size(); ++i)
            reconcile(lhs_added.row(i));
        if (m_plan.lhs_keys().empty())
            reconcile_matches(rhs_added.empty() ? rhs_removed : rhs_added);
        else
        {
            reconcile_matches(rhs_removed);
            reconcile_matches(rhs_added);
        }
        detail::apply_delta(m_delta, m_result);
        m_initialized = true;
    }

    template<typename>
    friend struct detail::NodeRules;

public:
    SemijoinEvaluator(JoinPlan plan, bool anti) :
        m_plan(std::move(plan)),
        m_anti(anti),
        m_lhs(m_plan.lhs_columns().span()),
        m_rhs(m_plan.rhs_columns().span()),
        m_result(m_plan.lhs_columns().span()),
        m_delta(m_plan.lhs_columns().span())
    {
    }

    /// Replace the baseline, retaining buffers, and clear the last output delta.
    template<RelationViewConcept L, RelationViewConcept R>
    void initialize(const L& lhs, const R& rhs)
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
        for (size_t i = 0; i < m_lhs.size(); ++i)
            if (kept(m_lhs.row(i)))
                m_result.insert(Row(m_lhs.row(i), m_lhs.columns().span()));
        m_initialized = true;
    }

    /// Each change describes actual set changes of that input. Reports the exact net change of the batch.
    template<RelationChange L, RelationChange R>
    void update(const L& lhs, const R& rhs)
    {
        detail::require_initialized(m_initialized);
        detail::require_change(lhs, m_plan.lhs_columns().span(), m_result, m_delta);
        detail::require_change(rhs, m_plan.rhs_columns().span(), m_result, m_delta);
        apply(lhs, rhs);
    }

    const Builder<Relation>& get_result() const&
    {
        detail::require_initialized(m_initialized);
        return m_result;
    }
    const Builder<Relation>& get_result() const&& = delete;
    const Delta& get_delta() const&
    {
        detail::require_initialized(m_initialized);
        return m_delta;
    }
    const Delta& get_delta() const&& = delete;

    size_t memory_usage() const noexcept
    {
        return m_lhs.memory_usage() + m_rhs.memory_usage() + m_result.memory_usage() + m_delta.memory_usage() + m_lhs_index.memory_usage()
               + m_rhs_index.memory_usage();
    }
};

}  // namespace ygg::database::incremental

#endif
