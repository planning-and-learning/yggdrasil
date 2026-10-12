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
#include "yggdrasil/database/semantics/search/backward_closure.hpp"
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
///  2. Search: A* (search/a_star.hpp); the estimate is the maximum of H, the learned distance and the closure's bound.
///  3. Bidirectionality: the backward closure (search/backward_closure.hpp), kept across calls.
///  4. Reuse. While the targets stay the same, a path tree (incremental/search/adaptive_path_tree.hpp) persists:
///     closed vertices learn distance bounds (GAA*), and a search stops at a stored vertex whose next chain still
///     exists and drops by one per edge (MPGAA*); edges are re-checked lazily after input changes.
///  5. Changes. bind() diffs the inputs. Removed edges keep every bound admissible. Added edges are found by seeding
///     the branches with the changed rows (delta rules), learned distances are repaired, and the backward closure
///     restarts. New targets, or changes the delta rules cannot express, reset the tree.
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

    size_t input_count() const noexcept { return m_edges.input_count(); }
    const Builder<Relation<Values>>& input(size_t slot) const { return m_edges.input(slot); }

    /// Binds the current inputs: input(slot) returns slot's relation view (see BoundQueryEvaluator::bind).
    template<typename Input>
    void bind(Input&& input, const std::vector<bool>& maybe_changed = {})
    {
        const auto changes = m_edges.bind(input, maybe_changed);
        // Seeds run in the new state of every input, so an edge built from several changed rows is found from any.
        for (const auto& change : changes)
            if (!m_reset_pending && !m_edges.seed(change, [&](std::span<const std::byte> edge) { return record_added(edge); }))
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
        if (!m_edges.bound())
            throw std::invalid_argument("Min distance: inputs are not bound.");
        database::detail::require_plan_columns(sources.columns().span(), m_source_columns);
        database::detail::require_plan_columns(targets.columns().span(), m_target_columns);
        ++m_statistics.searches;
        auto oracle = Oracle(m_edges, m_forward, m_backward, m_check, m_target_offset, m_statistics.edge_checks);
        if (!(m_active && m_targets.same(targets)) || m_reset_pending)
            reset(targets, heuristic);
        else if (!m_added.empty())
        {
            // Added edges can shorten distances: learned ones are repaired, the backward closure restarts.
            m_statistics.repaired += m_tree.repair(m_added, oracle, heuristic);
            restart_closure(heuristic);
        }
        m_added.clear();
        auto search = AStar<Oracle, H>(oracle, m_tree.tree(), m_closure, heuristic, m_targets.vertices(), m_vertex_size, m_edges.version());
        const auto distance = search.run(sources);
        m_statistics.expanded += search.counters().expanded;
        m_statistics.tree_hits += search.counters().tree_hits;
        m_statistics.backward_vertices += search.counters().backward_vertices;
        m_statistics.closure_dead += search.counters().closure_dead;
        return distance;
    }

    const MinDistanceStatistics& statistics() const noexcept { return m_statistics; }

private:
    MinDistance(BoundQueryEvaluator<Values> edges, const DistancePlan<Values>& plan) :
        m_k(plan.arity()),
        m_vertex_size(plan.tuple_size()),
        m_target_offset(plan.edge_columns()[plan.arity()].offset),
        m_source_columns(plan.source_columns().span().begin(), plan.source_columns().span().end()),
        m_target_columns(plan.target_columns().span().begin(), plan.target_columns().span().end()),
        m_edges(std::move(edges)),
        m_forward(m_edges.prepare(database::detail::column_range(0, m_k))),
        m_backward(m_edges.prepare(database::detail::column_range(m_k, 2 * m_k))),
        m_check(m_edges.prepare(database::detail::column_range(0, 2 * m_k))),
        m_tree(plan.tuple_size()),
        m_targets(plan.tuple_size()),
        m_added(plan.tuple_size())
    {
    }

    using Oracle = database::detail::EdgeOracle<BoundQueryEvaluator<Values>>;

    size_t m_k;
    size_t m_vertex_size;
    size_t m_target_offset;
    std::vector<ColumnLayout> m_source_columns;
    std::vector<ColumnLayout> m_target_columns;
    BoundQueryEvaluator<Values> m_edges;
    size_t m_forward;
    size_t m_backward;
    size_t m_check;
    AdaptivePathTree m_tree;
    BackwardClosure m_closure;
    database::detail::TargetSet m_targets;
    EdgeList m_added;  ///< Edges the last bind() can have added.
    bool m_active = false;
    bool m_reset_pending = true;
    MinDistanceStatistics m_statistics;

    bool record_added(std::span<const std::byte> edge)
    {
        m_added.add(edge.first(m_target_offset), edge.subspan(m_target_offset));
        return true;
    }

    template<RelationViewConcept<Values> T>
    void reset(const T& targets, const H& heuristic)
    {
        ++m_statistics.resets;
        m_tree.clear();
        m_targets.assign(targets);
        m_active = true;
        m_reset_pending = false;
        restart_closure(heuristic);
    }

    void restart_closure(const H& heuristic) { m_closure.start(m_targets.ensure_in(m_tree.tree(), heuristic)); }
};

}  // namespace ygg::database::incremental

#endif
