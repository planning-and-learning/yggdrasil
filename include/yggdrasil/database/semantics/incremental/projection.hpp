/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_INCREMENTAL_PROJECTION_HPP_
#define YGG_DATABASE_SEMANTICS_INCREMENTAL_PROJECTION_HPP_

#include "yggdrasil/database/semantics/incremental/delta.hpp"
#include "yggdrasil/database/semantics/operations.hpp"
#include "yggdrasil/database/semantics/zset.hpp"

#include <utility>

namespace ygg::database::incremental
{

/// Maintains set projection and the number of input rows supporting each output: a Z-set whose rows are the result.
/// Support counting follows Gupta, Mumick and Subrahmanian, "Maintaining Views
/// Incrementally" (SIGMOD 1993), Sections 4-5: https://doi.org/10.1145/170035.170066;
/// the delta inspects final supports only (DBSP, Proposition 4.7).
/// Inputs are borrowed only for each call and must not alias this evaluator's
/// result/delta or the sequential caller workspace. Results and deltas are borrowed
/// until the next mutation. Rejected schema/overlap checks preserve the previous
/// evaluation. After a failure during mutation, results and deltas are unavailable
/// until initialize().
class ProjectionEvaluator
{
    ProjectionPlan m_plan;
    ZSet m_supports;
    Delta m_delta;
    /// The removals per output row of the change update() validates; kept for its storage.
    ZSet m_removals;
    bool m_initialized = false;

    std::span<const std::byte> project_row(std::span<const std::byte> input, Workspace& workspace) const;
    /// Throws unless every removed row's projection has as many witnesses as removals.
    template<RelationViewConcept V>
    void require_witnesses(const V& removed, Workspace& workspace);
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
    /// Schema, overlap, aliasing and witness errors are rejected before changing the result.
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
    detail::require_unaliased(input, m_supports.rows(), m_delta);
    m_initialized = false;
    m_supports.clear();
    m_delta.clear();
    for (size_t i = 0; i < input.size(); ++i)
        m_supports.add(project_row(input.row(i), workspace), 1);
    m_initialized = true;
}

template<RelationChange C>
void ProjectionEvaluator::update(const C& change, Workspace& workspace)
{
    detail::require_initialized(m_initialized);
    detail::require_change(change, m_plan.input_columns().span(), m_supports.rows(), m_delta);
    require_witnesses(std::get<1>(change), workspace);
    apply(change, workspace);
}

template<RelationViewConcept V>
void ProjectionEvaluator::require_witnesses(const V& removed, Workspace& workspace)
{
    m_removals.clear();
    for (size_t i = 0; i < removed.size(); ++i)
        m_removals.add(project_row(removed.row(i), workspace), 1);
    for (size_t i = 0; i < m_removals.size(); ++i)
        if (m_removals.weight(i) > m_supports.weight(m_removals.row(i)))
            throw std::invalid_argument("Incremental projection: removal has no supporting row.");
}

template<RelationChange C>
void ProjectionEvaluator::apply(const C& change, Workspace& workspace)
{
    const auto& [added, removed] = change;
    m_initialized = false;
    m_delta.clear();
    // Additions first: a support then reaches zero only if it ends at zero, so an output whose witness is replaced keeps
    // its position, and no row is both added and removed.
    for (size_t i = 0; i < added.size(); ++i)
    {
        const auto row = project_row(added.row(i), workspace);
        if (m_supports.add(row, 1) == 1)
            m_delta.added.insert(Row(row, m_plan.output_columns().span()));
    }
    for (size_t i = 0; i < removed.size(); ++i)
    {
        const auto row = project_row(removed.row(i), workspace);
        if (m_supports.add(row, -1) == 0)
            m_delta.removed.insert(Row(row, m_plan.output_columns().span()));
    }
    m_initialized = true;
}

}  // namespace ygg::database::incremental

#endif
