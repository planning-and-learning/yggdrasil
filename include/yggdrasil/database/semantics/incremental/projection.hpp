/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_INCREMENTAL_PROJECTION_HPP_
#define YGG_DATABASE_SEMANTICS_INCREMENTAL_PROJECTION_HPP_

#include "yggdrasil/database/semantics/incremental/delta.hpp"
#include "yggdrasil/database/semantics/operations.hpp"

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
/// evaluation. After a failure during mutation, results and deltas are unavailable
/// until initialize().
template<ColumnTypes Values = DefaultColumnTypes>
class ProjectionEvaluator
{
    ProjectionPlan<Values> m_plan;
    Builder<Relation<Values>> m_result;
    std::vector<size_t> m_supports;
    Delta<Values> m_delta;
    std::vector<uint_t> m_pending_removals;
    bool m_initialized = false;

    std::span<const std::byte> project_row(std::span<const std::byte> input, Workspace<Values>& workspace) const;
    bool add(std::span<const std::byte> row);
    void remove(std::span<const std::byte> row);
    /// Applies a change that is known to be valid.
    template<RelationChange<Values> C>
    void apply(const C& change, Workspace<Values>& workspace);

    friend struct detail::NodeRules<Values, QueryProjectTag>;

public:
    explicit ProjectionEvaluator(ProjectionPlan<Values> plan);

    /// Replace the baseline, retaining buffers, and clear the last output delta.
    template<RelationViewConcept<Values> V>
    void initialize(const V& input, Workspace<Values>& workspace);

    /// The change must describe an actual change to the initialized input set.
    /// Schema, overlap, and aliasing errors are rejected before changing the result.
    template<RelationChange<Values> C>
    void update(const C& change, Workspace<Values>& workspace);

    const Builder<Relation<Values>>& get_result() const&;
    const Builder<Relation<Values>>& get_result() const&& = delete;
    const Delta<Values>& get_delta() const&;
    const Delta<Values>& get_delta() const&& = delete;

    /// Retained working storage; excludes the immutable plan and caller workspace.
    size_t memory_usage() const noexcept;
};

template<ColumnTypes Values>
std::span<const std::byte> ProjectionEvaluator<Values>::project_row(std::span<const std::byte> input, Workspace<Values>& workspace) const
{
    auto& row = workspace.row;
    row.clear();
    row.reserve(m_plan.output_columns().row_size());
    database::detail::append_fields(row, input, m_plan.positions());
    return row;
}

template<ColumnTypes Values>
bool ProjectionEvaluator<Values>::add(std::span<const std::byte> row)
{
    const auto index = m_result.insert(Row<Values>(row, m_plan.output_columns().span()));
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

template<ColumnTypes Values>
void ProjectionEvaluator<Values>::remove(std::span<const std::byte> row)
{
    const auto index = m_result.find(Row<Values>(row, m_plan.output_columns().span()));
    if (!index || m_supports[*index] == 0)
        throw std::invalid_argument("Incremental projection: removal has no supporting row.");
    if (--m_supports[*index] == 0)
        m_pending_removals.push_back(*index);
}

template<ColumnTypes Values>
ProjectionEvaluator<Values>::ProjectionEvaluator(ProjectionPlan<Values> plan) :
    m_plan(std::move(plan)),
    m_result(m_plan.output_columns().span()),
    m_delta(m_plan.output_columns().span())
{
}

template<ColumnTypes Values>
template<RelationViewConcept<Values> V>
void ProjectionEvaluator<Values>::initialize(const V& input, Workspace<Values>& workspace)
{
    database::detail::require_plan_columns(input.columns().span(), m_plan.input_columns().span());
    detail::require_unaliased(input, m_result, m_delta);
    m_initialized = false;
    m_result.clear();
    m_supports.clear();
    m_delta.clear();
    m_pending_removals.clear();
    for (size_t i = 0; i < input.size(); ++i)
        add(project_row(input.row(i), workspace));
    m_initialized = true;
}

template<ColumnTypes Values>
template<RelationChange<Values> C>
void ProjectionEvaluator<Values>::update(const C& change, Workspace<Values>& workspace)
{
    detail::require_initialized(m_initialized);
    detail::require_change(change, m_plan.input_columns().span(), m_result, m_delta);
    apply(change, workspace);
}

template<ColumnTypes Values>
template<RelationChange<Values> C>
void ProjectionEvaluator<Values>::apply(const C& change, Workspace<Values>& workspace)
{
    const auto& [added, removed] = change;
    m_initialized = false;
    m_delta.clear();
    m_pending_removals.clear();
    for (size_t i = 0; i < removed.size(); ++i)
        remove(project_row(removed.row(i), workspace));
    for (size_t i = 0; i < added.size(); ++i)
    {
        const auto row = project_row(added.row(i), workspace);
        if (add(row))
            m_delta.added.insert(Row<Values>(row, m_plan.output_columns().span()));
    }
    // Inspect final support counts only (DBSP, Proposition 4.7). A replaced
    // witness revives its existing row without an erase/insert cycle.
    // Descending positions keep pending indices valid during swap-and-pop.
    std::ranges::sort(m_pending_removals, std::greater {});
    for (const auto position : m_pending_removals)
        if (m_supports[position] == 0)
        {
            m_delta.removed.insert(Row<Values>(m_result.row(position), m_plan.output_columns().span()));
            m_result.erase(position);
            m_supports[position] = m_supports.back();
            m_supports.pop_back();
        }
    m_initialized = true;
}

template<ColumnTypes Values>
const Builder<Relation<Values>>& ProjectionEvaluator<Values>::get_result() const&
{
    detail::require_initialized(m_initialized);
    return m_result;
}

template<ColumnTypes Values>
const Delta<Values>& ProjectionEvaluator<Values>::get_delta() const&
{
    detail::require_initialized(m_initialized);
    return m_delta;
}

template<ColumnTypes Values>
size_t ProjectionEvaluator<Values>::memory_usage() const noexcept
{
    return m_result.memory_usage() + m_delta.memory_usage() + m_supports.capacity() * sizeof(size_t) + m_pending_removals.capacity() * sizeof(uint_t);
}

}  // namespace ygg::database::incremental

#endif
