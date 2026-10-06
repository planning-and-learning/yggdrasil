/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_INCREMENTAL_PROJECTION_HPP_
#define YGG_DATABASE_INCREMENTAL_PROJECTION_HPP_

#include "yggdrasil/database/incremental/delta.hpp"
#include "yggdrasil/database/operations.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

namespace ygg::database::incremental
{

/// Maintains set projection and the number of input rows supporting each output.
/// Support counting follows Gupta, Mumick and Subrahmanian, "Maintaining Views
/// Incrementally" (SIGMOD 1993), Sections 4-5: https://doi.org/10.1145/170035.170066.
/// Inputs are borrowed only for each call and must not alias this evaluator's
/// result/delta or the sequential caller workspace. Results and deltas are borrowed
/// until the next mutation. Rejected schema/overlap checks preserve the previous
/// evaluation. A failure during mutation leaves partial contents; call initialize()
/// before using them.
template<TriviallyCopyable T = uint_t>
class ProjectionEvaluator
{
    ProjectionPlan m_plan;
    Builder<Relation<T>> m_result;
    std::vector<size_t> m_supports;
    Delta<T> m_delta;
    std::vector<uint_t> m_pending_removals;
    bool m_initialized = false;

    std::span<const T> project_row(std::span<const T> input, Workspace<T>& workspace) const;
    bool add(std::span<const T> row);
    void remove(std::span<const T> row);

public:
    explicit ProjectionEvaluator(ProjectionPlan plan);

    /// Replace the baseline, retaining buffers, and clear the last output delta.
    template<RelationViewConcept<T> V>
    void initialize(const V& input, Workspace<T>& workspace);

    /// The two inputs must describe actual changes to the initialized input set.
    /// Schema and overlap errors are rejected before changing the result.
    template<RelationViewConcept<T> A, RelationViewConcept<T> R>
    void update(const A& added, const R& removed, Workspace<T>& workspace);

    const Builder<Relation<T>>& get_result() const& noexcept;
    const Builder<Relation<T>>& get_result() const&& = delete;
    const Delta<T>& get_delta() const& noexcept;
    const Delta<T>& get_delta() const&& = delete;

    /// Retained working storage; excludes the immutable plan and caller workspace.
    size_t memory_usage() const noexcept;
};

template<TriviallyCopyable T>
std::span<const T> ProjectionEvaluator<T>::project_row(std::span<const T> input, Workspace<T>& workspace) const
{
    auto& row = workspace.row;
    row.clear();
    for (const auto position : m_plan.positions())
        row.push_back(input[position]);
    return row;
}

template<TriviallyCopyable T>
bool ProjectionEvaluator<T>::add(std::span<const T> row)
{
    const auto index = m_result.insert(row);
    if (index == m_supports.size())
    {
        m_supports.push_back(1);
        return true;
    }
    auto& count = m_supports[index];
    if (count == std::numeric_limits<size_t>::max())
        throw std::overflow_error("Incremental projection: support count overflow.");
    ++count;
    return false;
}

template<TriviallyCopyable T>
void ProjectionEvaluator<T>::remove(std::span<const T> row)
{
    const auto index = m_result.find(row);
    if (!index || m_supports[*index] == 0)
        throw std::invalid_argument("Incremental projection: removal has no supporting row.");
    if (--m_supports[*index] == 0)
        m_pending_removals.push_back(*index);
}

template<TriviallyCopyable T>
ProjectionEvaluator<T>::ProjectionEvaluator(ProjectionPlan plan) :
    m_plan(std::move(plan)),
    m_result(m_plan.output_columns().span()),
    m_delta(m_plan.output_columns().span())
{
}

template<TriviallyCopyable T>
template<RelationViewConcept<T> V>
void ProjectionEvaluator<T>::initialize(const V& input, Workspace<T>& workspace)
{
    database::detail::require_plan_columns(input.columns().span(), m_plan.input_columns().span());
    m_initialized = false;
    m_result.clear();
    m_supports.clear();
    m_delta.clear();
    m_pending_removals.clear();
    for (size_t i = 0; i < input.size(); ++i)
        add(project_row(input.row(i), workspace));
    m_initialized = true;
}

template<TriviallyCopyable T>
template<RelationViewConcept<T> A, RelationViewConcept<T> R>
void ProjectionEvaluator<T>::update(const A& added, const R& removed, Workspace<T>& workspace)
{
    if (!m_initialized)
        throw std::logic_error("Incremental projection: initialize before updating.");
    detail::require_delta<T>(added, removed, m_plan.input_columns().span());
    m_initialized = false;
    m_delta.clear();
    m_pending_removals.clear();
    for (size_t i = 0; i < removed.size(); ++i)
        remove(project_row(removed.row(i), workspace));
    for (size_t i = 0; i < added.size(); ++i)
    {
        const auto row = project_row(added.row(i), workspace);
        if (add(row))
            m_delta.added.insert(row);
    }
    // Inspect final support counts only (DBSP, Proposition 4.7). A replaced
    // witness revives its existing row without an erase/insert cycle.
    // Descending positions keep pending indices valid during swap-and-pop.
    std::ranges::sort(m_pending_removals, std::greater {});
    for (const auto position : m_pending_removals)
        if (m_supports[position] == 0)
        {
            m_delta.removed.insert(m_result.row(position));
            m_result.erase(position);
            m_supports[position] = m_supports.back();
            m_supports.pop_back();
        }
    m_initialized = true;
}

template<TriviallyCopyable T>
const Builder<Relation<T>>& ProjectionEvaluator<T>::get_result() const& noexcept
{
    return m_result;
}

template<TriviallyCopyable T>
const Delta<T>& ProjectionEvaluator<T>::get_delta() const& noexcept
{
    return m_delta;
}

template<TriviallyCopyable T>
size_t ProjectionEvaluator<T>::memory_usage() const noexcept
{
    return m_result.memory_usage() + m_delta.memory_usage() + m_supports.capacity() * sizeof(size_t)
           + m_pending_removals.capacity() * sizeof(uint_t);
}

}  // namespace ygg::database::incremental

#endif
