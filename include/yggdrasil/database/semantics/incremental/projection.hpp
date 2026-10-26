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
class ProjectionEvaluator
{
    ProjectionPlan m_plan;
    Builder<Relation> m_result;
    std::vector<size_t> m_supports;
    Delta m_delta;
    std::vector<uint_t> m_pending_removals;
    bool m_initialized = false;

    std::span<const std::byte> project_row(std::span<const std::byte> input, Workspace& workspace) const;
    bool add(std::span<const std::byte> row);
    void remove(std::span<const std::byte> row);
    /// Applies a change that is known to be valid.
    template<RelationChange C>
    void apply(const C& change, Workspace& workspace);

    friend struct detail::NodeRules<QueryProjectTag>;

public:
    explicit ProjectionEvaluator(ProjectionPlan plan);

    /// Replace the baseline, retaining buffers, and clear the last output delta.
    template<RelationViewConcept V>
    void initialize(const V& input, Workspace& workspace);

    /// The change must describe an actual change to the initialized input set.
    /// Schema, overlap, and aliasing errors are rejected before changing the result.
    template<RelationChange C>
    void update(const C& change, Workspace& workspace);

    [[nodiscard]] const Builder<Relation>& get_result() const&;
    [[nodiscard]] const Builder<Relation>& get_result() const&& = delete;
    [[nodiscard]] const Delta& get_delta() const&;
    [[nodiscard]] const Delta& get_delta() const&& = delete;

    /// Retained working storage; excludes the immutable plan and caller workspace.
    [[nodiscard]] size_t memory_usage() const noexcept;
};

template<RelationViewConcept V>
void ProjectionEvaluator::initialize(const V& input, Workspace& workspace)
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

template<RelationChange C>
void ProjectionEvaluator::update(const C& change, Workspace& workspace)
{
    detail::require_initialized(m_initialized);
    detail::require_change(change, m_plan.input_columns().span(), m_result, m_delta);
    apply(change, workspace);
}

template<RelationChange C>
void ProjectionEvaluator::apply(const C& change, Workspace& workspace)
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
            m_delta.added.insert(Row(row, m_plan.output_columns().span()));
    }
    // Inspect final support counts only (DBSP, Proposition 4.7). A replaced
    // witness revives its existing row without an erase/insert cycle.
    // Descending positions keep pending indices valid during swap-and-pop.
    std::ranges::sort(m_pending_removals, std::greater {});
    for (const auto position : m_pending_removals)
        if (m_supports[position] == 0)
        {
            m_delta.removed.insert(Row(m_result.row(position), m_plan.output_columns().span()));
            m_result.erase(position);
            m_supports[position] = m_supports.back();
            m_supports.pop_back();
        }
    m_initialized = true;
}

}  // namespace ygg::database::incremental

#endif
