/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_INCREMENTAL_MIN_DISTANCE_HPP_
#define YGG_DATABASE_SEMANTICS_INCREMENTAL_MIN_DISTANCE_HPP_

#include "yggdrasil/database/semantics/details/min_distance.hpp"
#include "yggdrasil/database/semantics/incremental/bound_evaluation.hpp"
#include "yggdrasil/database/semantics/incremental/search/adaptive_path_tree.hpp"
#include "yggdrasil/database/semantics/min_distance.hpp"
#include "yggdrasil/database/semantics/search/a_star.hpp"
#include "yggdrasil/database/semantics/search/backward_frontier.hpp"
#include "yggdrasil/database/semantics/search/concepts.hpp"

#include <cstddef>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ygg::database::incremental
{

/// Exact minimum distance from any source to any target, or uint_t's maximum if none is reachable, kept across calls
/// while the inputs change. Edges are given by a query over inputs, evaluated on demand and never materialized, or as a
/// relation, which is a query of one input.
///
/// Method (steps 1-3 as in database::MinDistance, min_distance.hpp):
///  1. Edges, by an incremental::BoundQueryEvaluator (incremental/bound_evaluation.hpp) over snapshots of the inputs.
///  2. Search: A* (search/a_star.hpp); the estimate is the maximum of H, the learned distance and the backward bound.
///  3. Bidirectionality: the backward frontier (search/backward_frontier.hpp), kept across calls.
///  4. Reuse. While the targets stay the same, the SearchTree persists: closed vertices learn distance bounds (GAA*),
///     and a search stops at a stored vertex whose next chain still exists and drops by one per edge (MPGAA*); edges
///     are re-checked lazily after input changes.
///  5. Changes. bind() diffs the inputs. Removed edges keep every bound admissible. Added edges are found by seeding
///     the branches with the changed rows (delta rules), learned distances are repaired (repair(),
///     incremental/search/adaptive_path_tree.hpp), and the backward frontier restarts. New targets, or changes the
///     delta rules cannot express, reset the tree.
///
/// Contract: H is consistent in every state while the targets stay unchanged (DistanceHeuristic).
///
/// Inputs are borrowed during bind() only.
template<ColumnTypes Values, DistanceHeuristic H = BlindDistanceHeuristic>
    requires ColumnValueFor<uint_t, Values>
class MinDistance
{
public:
    /// Edges given by a query over inputs. `edges` has the plan's edge columns; `static_slots` marks its inputs that
    /// never change after the first bind().
    MinDistance(QueryView<Values> edges, const DistancePlan<Values>& plan, const std::vector<bool>& static_slots = {}) :
        MinDistance(BoundQueryEvaluator<Values>(database::detail::checked_edges(edges, plan), static_slots), plan)
    {
    }

    /// Edges given as a relation with the plan's edge columns, bound by bind_edges().
    explicit MinDistance(const DistancePlan<Values>& plan, bool static_edges = false) :
        MinDistance(database::detail::relation_edges<BoundQueryEvaluator<Values>>(plan, static_edges), plan)
    {
    }

    size_t input_count() const noexcept { return m_edges.evaluator().input_count(); }
    const Builder<Relation<Values>>& input(size_t slot) const { return m_edges.evaluator().input(slot); }

    /// Binds the current inputs: input(slot) returns slot's relation view (see BoundQueryEvaluator::bind).
    template<typename Input>
    void bind(Input&& input, const std::vector<bool>& maybe_changed = {})
    {
        const auto changes = m_edges.evaluator().bind(input, maybe_changed);
        // Seeds run in the new state of every input, so an edge built from several changed rows is found from any.
        for (const auto& change : changes)
            if (!m_reset_pending && !m_edges.evaluator().seed(change, [&](std::span<const std::byte> edge) { return record_added(edge); }))
                m_reset_pending = true;
        if (m_reset_pending)
            m_added.clear();
    }

    /// Binds the edge relation of a relation-edged instance; unchanged edges are not compared.
    template<RelationViewConcept<Values> E>
    void bind_edges(const E& edges, bool maybe_changed = true)
    {
        bind([&](size_t) -> const E& { return edges; }, std::vector<bool> { maybe_changed });
    }

    /// The minimum distance over the edges of the bound inputs. H must stay the same function while the targets are
    /// unchanged.
    template<RelationViewConcept<Values> S, RelationViewConcept<Values> T>
    uint_t min_distance(const S& sources, const T& targets, const H& heuristic = {})
    {
        if (!m_edges.evaluator().bound())
            throw std::invalid_argument("Min distance: inputs are not bound.");
        m_edges.require_columns(sources, targets);
        if (m_reset_pending || !m_targets.same(targets))
            reset(targets, heuristic);
        else if (!m_added.empty())
        {
            // Added edges can shorten distances: learned ones are repaired, the backward frontier restarts.
            m_statistics.repaired += repair(m_tree, m_added, m_edges, heuristic);
            restart_backward(heuristic);
        }
        m_added.clear();
        auto search = AStar<Edges, H>(m_edges, m_tree, m_backward, heuristic, m_targets.vertices(), m_edges.vertex_size(), m_edges.evaluator().version());
        const auto distance = search.run(sources);
        m_statistics += search.counters();
        return distance;
    }

    const MinDistanceStatistics& statistics() const noexcept { return m_statistics; }

private:
    using Edges = database::detail::EdgeQuery<BoundQueryEvaluator<Values>, Values>;

    MinDistance(BoundQueryEvaluator<Values> edges, const DistancePlan<Values>& plan) :
        m_edges(std::move(edges), plan),
        m_tree(plan.tuple_size()),
        m_targets(plan.tuple_size()),
        m_added(plan.tuple_size())
    {
    }

    Edges m_edges;
    SearchTree m_tree;
    BackwardFrontier m_backward;
    database::detail::TargetSet m_targets;
    EdgeList m_added;  ///< Edges the last bind() can have added.
    bool m_reset_pending = true;
    MinDistanceStatistics m_statistics;

    bool record_added(std::span<const std::byte> edge)
    {
        m_added.add(edge.first(m_edges.target_offset()), edge.subspan(m_edges.target_offset()));
        return true;
    }

    template<RelationViewConcept<Values> T>
    void reset(const T& targets, const H& heuristic)
    {
        m_tree.clear();
        m_targets.assign(targets);
        m_reset_pending = false;
        restart_backward(heuristic);
    }

    void restart_backward(const H& heuristic) { m_backward.start(m_targets.ensure_in(m_tree, heuristic)); }
};

}  // namespace ygg::database::incremental

#endif
