/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_INCREMENTAL_JOIN_HPP_
#define YGG_DATABASE_INCREMENTAL_JOIN_HPP_

#include "yggdrasil/containers/unordered_multi_map.hpp"
#include "yggdrasil/database/incremental/delta.hpp"
#include "yggdrasil/database/operations.hpp"

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
/// Rejected schema/overlap checks preserve the previous evaluation. A failure
/// during mutation leaves partial contents; call initialize() before using them.
/// No repository publication is required.
template<TriviallyCopyable T = uint_t>
class JoinEvaluator
{
    JoinPlan m_plan;
    Builder<Relation<T>> m_lhs;
    Builder<Relation<T>> m_rhs;
    UnorderedMultiMap<hash_t, size_t> m_lhs_index;
    UnorderedMultiMap<hash_t, size_t> m_rhs_index;
    Builder<Relation<T>> m_result;
    Delta<T> m_delta;
    bool m_initialized = false;

    template<RelationViewConcept<T> V>
    static void insert_rows(const V& input, Builder<Relation<T>>& rows, std::span<const size_t> keys, UnorderedMultiMap<hash_t, size_t>& index);

    template<RelationViewConcept<T> V>
    static void erase_rows(const V& input, Builder<Relation<T>>& rows, std::span<const size_t> keys, UnorderedMultiMap<hash_t, size_t>& index);

    template<RelationViewConcept<T> L, RelationViewConcept<T> R>
    void join(const L& lhs, const R& rhs, bool build_left, Builder<Relation<T>>& output, Workspace<T>& workspace) const;

public:
    explicit JoinEvaluator(JoinPlan plan);

    /// Replace the baseline, retaining buffers, and clear the last output delta.
    template<RelationViewConcept<T> L, RelationViewConcept<T> R>
    void initialize(const L& lhs, const R& rhs, Workspace<T>& workspace);

    /// Each pair describes actual set changes in that input's original column
    /// order. Pass empty changes for an unchanged side; swap added/removed on
    /// both sides to undo. Reports the exact net change of the complete batch.
    template<RelationViewConcept<T> LA, RelationViewConcept<T> LR, RelationViewConcept<T> RA, RelationViewConcept<T> RR>
    void update(const LA& lhs_added, const LR& lhs_removed, const RA& rhs_added, const RR& rhs_removed, Workspace<T>& workspace);

    const Builder<Relation<T>>& get_result() const& noexcept;
    const Builder<Relation<T>>& get_result() const&& = delete;
    const Delta<T>& get_delta() const& noexcept;
    const Delta<T>& get_delta() const&& = delete;

    /// Retained inputs, result, delta and indexes; excludes plan and caller workspace.
    size_t memory_usage() const noexcept;
};

template<TriviallyCopyable T>
template<RelationViewConcept<T> V>
void JoinEvaluator<T>::insert_rows(const V& input, Builder<Relation<T>>& rows, std::span<const size_t> keys, UnorderedMultiMap<hash_t, size_t>& index)
{
    if (!keys.empty())
        index.reserve(rows.size() + input.size());
    for (size_t i = 0; i < input.size(); ++i)
    {
        const auto row = input.row(i);
        const auto previous_size = rows.size();
        const auto position = rows.insert(row);
        if (position != previous_size)
            throw std::invalid_argument("Incremental join: added row is already present in the input.");
        if (!keys.empty())
            index.insert(ygg::hash_range(database::detail::join_key_values(row, keys)), position);
    }
}

template<TriviallyCopyable T>
template<RelationViewConcept<T> V>
void JoinEvaluator<T>::erase_rows(const V& input, Builder<Relation<T>>& rows, std::span<const size_t> keys, UnorderedMultiMap<hash_t, size_t>& index)
{
    for (size_t i = 0; i < input.size(); ++i)
    {
        const auto position = rows.find(input.row(i));
        if (!position)
            throw std::invalid_argument("Incremental join: removed row is absent from the input.");
        const auto last = rows.size() - 1;
        if (keys.empty())
        {
            rows.erase(*position);
            continue;
        }
        const auto removed_key = ygg::hash_range(database::detail::join_key_values(rows.row(*position), keys));
        const auto moved_key = ygg::hash_range(database::detail::join_key_values(rows.row(last), keys));
        rows.erase(*position);
        [[maybe_unused]] const auto erased = index.erase(removed_key, *position);
        assert(erased);
        if (*position != last)
        {
            [[maybe_unused]] const auto replaced = index.replace(moved_key, last, *position);
            assert(replaced);
        }
    }
}

template<TriviallyCopyable T>
template<RelationViewConcept<T> L, RelationViewConcept<T> R>
void JoinEvaluator<T>::join(const L& lhs, const R& rhs, bool build_left, Builder<Relation<T>>& output, Workspace<T>& workspace) const
{
    database::detail::join_rows<T>(lhs,
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

template<TriviallyCopyable T>
JoinEvaluator<T>::JoinEvaluator(JoinPlan plan) :
    m_plan(std::move(plan)),
    m_lhs(m_plan.lhs_columns().span()),
    m_rhs(m_plan.rhs_columns().span()),
    m_result(m_plan.output_columns().span()),
    m_delta(m_plan.output_columns().span())
{
}

template<TriviallyCopyable T>
template<RelationViewConcept<T> L, RelationViewConcept<T> R>
void JoinEvaluator<T>::initialize(const L& lhs, const R& rhs, Workspace<T>& workspace)
{
    database::detail::require_plan_columns(lhs.columns().span(), m_plan.lhs_columns().span());
    database::detail::require_plan_columns(rhs.columns().span(), m_plan.rhs_columns().span());
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

template<TriviallyCopyable T>
template<RelationViewConcept<T> LA, RelationViewConcept<T> LR, RelationViewConcept<T> RA, RelationViewConcept<T> RR>
void JoinEvaluator<T>::update(const LA& lhs_added, const LR& lhs_removed, const RA& rhs_added, const RR& rhs_removed, Workspace<T>& workspace)
{
    if (!m_initialized)
        throw std::logic_error("Incremental join: initialize before updating.");
    detail::require_delta<T>(lhs_added, lhs_removed, m_plan.lhs_columns().span());
    detail::require_delta<T>(rhs_added, rhs_removed, m_plan.rhs_columns().span());
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

    for (size_t i = 0; i < m_delta.removed.size(); ++i)
    {
        const auto position = m_result.find(m_delta.removed.row(i));
        assert(position);
        m_result.erase(*position);
    }
    for (size_t i = 0; i < m_delta.added.size(); ++i)
        m_result.insert(m_delta.added.row(i));
    m_initialized = true;
}

template<TriviallyCopyable T>
const Builder<Relation<T>>& JoinEvaluator<T>::get_result() const& noexcept
{
    return m_result;
}

template<TriviallyCopyable T>
const Delta<T>& JoinEvaluator<T>::get_delta() const& noexcept
{
    return m_delta;
}

template<TriviallyCopyable T>
size_t JoinEvaluator<T>::memory_usage() const noexcept
{
    return m_lhs.memory_usage() + m_rhs.memory_usage() + m_result.memory_usage() + m_delta.memory_usage() + m_lhs_index.memory_usage()
           + m_rhs_index.memory_usage();
}

}  // namespace ygg::database::incremental

#endif
