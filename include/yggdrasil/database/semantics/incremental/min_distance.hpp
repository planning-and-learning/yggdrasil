/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_INCREMENTAL_MIN_DISTANCE_HPP_
#define YGG_DATABASE_SEMANTICS_INCREMENTAL_MIN_DISTANCE_HPP_

#include "yggdrasil/database/semantics/details/min_distance.hpp"
#include "yggdrasil/database/semantics/incremental/delta.hpp"
#include "yggdrasil/database/semantics/incremental/bound/query_evaluation.hpp"
#include "yggdrasil/database/semantics/incremental/search/tree_repair.hpp"
#include "yggdrasil/database/semantics/min_distance.hpp"
#include "yggdrasil/database/semantics/search/a_star.hpp"
#include "yggdrasil/database/semantics/search/backward_frontier.hpp"
#include "yggdrasil/database/semantics/search/concepts.hpp"

#include <concepts>
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
///  1. Edges, by an incremental::bound::QueryEvaluator (incremental/bound/query_evaluation.hpp) over snapshots of the
///     inputs.
///  2. Search: A* (search/a_star.hpp); the estimate is the maximum of H, the learned distance and the backward bound.
///  3. Bidirectionality: the backward frontier (search/backward_frontier.hpp), kept across calls.
///  4. Reuse. While the targets stay the same, the SearchTree persists: closed vertices learn distance bounds (GAA*),
///     and a search stops at a stored vertex whose next chain still exists and drops by one per edge (MPGAA*); edges
///     are re-checked lazily after input changes that can remove edges (incremental::bound::QueryEvaluator::version()).
///  5. Changes. initialize() binds a new baseline; bind() diffs the inputs, or update() takes their exact changes.
///     Removed edges keep every bound admissible. Added edges are found by seeding the branches with the changed
///     rows (delta rules), and learned distances are repaired (TreeRepair, incremental/search/tree_repair.hpp). The
///     backward frontier's bounds b (the level of a found vertex, otherwise one beyond the last level, or infinity once
///     exhausted) are consistent on the old edges, so they stay valid unless an added edge u -> v has b(u) > b(v) + 1;
///     only then does the frontier restart, as only edges that break consistency need work in LPA* (Koenig, Likhachev &
///     Furcy, "Lifelong Planning A*", AIJ 2004). Levels are exact before removals and lower bounds after them. New
///     targets, or changes the delta rules cannot express, reset the tree. A change that throws also resets it.
///
/// Contract: H is consistent in every state while the targets stay unchanged (DistanceHeuristic).
///
/// Inputs and changes are borrowed during bind() and update() only.
template<DistanceHeuristic H = BlindDistanceHeuristic>
class MinDistance
{
public:
    /// Edges given by a query over inputs. `edges` has the plan's edge columns; `static_slots` marks its inputs that
    /// never change after the first bind().
    MinDistance(QueryView<> edges, const DistancePlan& plan, const std::vector<bool>& static_slots = {}) :
        MinDistance(incremental::bound::QueryEvaluator(database::detail::checked_edges(edges, plan), static_slots), plan)
    {
    }

    /// Edges given as a relation with the plan's edge columns, bound by bind_edges().
    explicit MinDistance(const DistancePlan& plan, bool static_edges = false) :
        MinDistance(database::detail::relation_edges<incremental::bound::QueryEvaluator>(plan, static_edges), plan)
    {
    }

    size_t input_count() const noexcept { return m_edges.evaluator().input_count(); }
    const Builder<Relation>& input(size_t slot) const { return m_edges.evaluator().input(slot); }

    /// Binds every input as a new baseline: input(slot) returns slot's relation view (see
    /// incremental::bound::QueryEvaluator::initialize). The stored search is discarded.
    template<SlotRelations Input>
    void initialize(const Input& input)
    {
        m_reset_pending = true;
        m_added.clear();
        m_edges.evaluator().initialize(input);
    }

    /// Diffs the current inputs against the bound ones: input(slot) returns slot's relation view (see
    /// incremental::bound::QueryEvaluator::bind).
    template<SlotRelations Input>
    void bind(const Input& input, const std::vector<bool>& maybe_changed = {})
    {
        auto& evaluator = m_edges.evaluator();
        guarded(
            [&]
            {
                // Seeds run in the new state of every input, so an edge built from several changed rows is found
                // from any.
                for (const auto slot : evaluator.bind(input, maybe_changed))
                    seed(slot, evaluator.change(slot).added, evaluator.change(slot).removed);
            });
    }

    /// Applies exact changes to bound inputs instead of diffing them: for each slot marked in `changed`, change(slot)
    /// has relation views `added` (rows not in the slot) and `removed` (rows in it).
    template<SlotChanges Change>
    void update(const Change& change, const std::vector<bool>& changed)
    {
        if (changed.size() > input_count())
            throw std::invalid_argument("Min distance: more changed slots than inputs.");
        auto& evaluator = m_edges.evaluator();
        guarded(
            [&]
            {
                for (size_t slot = 0; slot < changed.size(); ++slot)
                    if (changed[slot])
                        evaluator.update(slot, change(slot).added, change(slot).removed);
                for (size_t slot = 0; slot < changed.size(); ++slot)
                    if (changed[slot])
                        seed(slot, change(slot).added, change(slot).removed);
            });
    }

    /// Binds the edge relation of a relation-edged instance; unchanged edges are not compared.
    template<RelationViewConcept E>
    void bind_edges(const E& edges, bool maybe_changed = true)
    {
        m_edges_changed.assign(1, maybe_changed);
        bind([&](size_t) -> const E& { return edges; }, m_edges_changed);
    }

    /// Sets the targets; different targets discard the stored search. Callers that know the targets are unchanged
    /// need not call it again.
    template<RelationViewConcept T>
    void set_targets(const T& targets)
    {
        database::detail::require_plan_columns(targets.columns().span(), m_plan.target_columns().span());
        if (m_targets_set && m_targets.same(targets))
            return;
        m_targets.assign(targets);
        m_targets_set = true;
        m_reset_pending = true;
    }

    /// The minimum distance over the edges of the bound inputs to the targets set last. H must stay the same function
    /// while the targets are unchanged.
    template<RelationViewConcept S>
    uint_t min_distance(const S& sources, const H& heuristic)
    {
        if (!m_edges.evaluator().bound() || !m_targets_set)
            throw std::invalid_argument("Min distance: inputs or targets are not set.");
        database::detail::require_plan_columns(sources.columns().span(), m_plan.source_columns().span());
        auto edges = m_edges.with_key({});
        if (m_reset_pending)
            reset(heuristic);
        else
        {
            // Added edges can shorten distances: learned ones are repaired.
            if (!m_added.empty())
                m_statistics.repaired += m_repair.run(m_tree, m_added, edges, heuristic);
            if (m_restart_backward)
                restart_backward(heuristic);
        }
        m_added.clear();
        m_restart_backward = false;
        auto search = AStar<typename Edges::Keyed, H>(edges, m_tree, m_backward, heuristic, m_targets.vertices(), m_search, m_edges.evaluator().version());
        const auto distance = search.run(sources);
        m_statistics += search.counters();
        return distance;
    }

    /// set_targets(targets), then min_distance(sources, heuristic).
    template<RelationViewConcept S, RelationViewConcept T>
    uint_t min_distance(const S& sources, const T& targets, const H& heuristic = {})
    {
        set_targets(targets);
        return min_distance(sources, heuristic);
    }

    const MinDistanceStatistics& statistics() const noexcept { return m_statistics; }

private:
    using Edges = database::detail::EdgeQuery<incremental::bound::QueryEvaluator>;

    MinDistance(incremental::bound::QueryEvaluator edges, const DistancePlan& plan) :
        m_plan(plan),
        m_edges(std::move(edges), plan.edge_columns().span(), 0, plan.arity()),
        m_tree(plan.tuple_size()),
        m_targets(plan.tuple_size()),
        m_added(plan.tuple_size()),
        m_search(plan.tuple_size())
    {
    }

    DistancePlan m_plan;
    Edges m_edges;
    SearchTree m_tree;
    BackwardFrontier m_backward;
    database::detail::TargetSet m_targets;
    EdgeList m_added;  ///< Edges bind() can have added whose source is stored: the only ones repair reads.
    bool m_restart_backward = false;  ///< An added edge breaks the backward frontier's bounds.
    bool m_reset_pending = true;
    bool m_targets_set = false;
    std::vector<bool> m_edges_changed;  ///< bind_edges()'s flag for bind(), kept to avoid allocations.
    TreeRepair m_repair;
    AStarStorage m_search;
    MinDistanceStatistics m_statistics;

    /// Runs a change of the inputs. A change that throws may have applied part of itself, so the stored bounds are not
    /// trusted afterwards: the next min_distance() resets the tree.
    template<typename F>
        requires std::invocable<const F&>
    void guarded(const F& change)
    {
        try
        {
            change();
        }
        catch (...)
        {
            m_reset_pending = true;
            m_added.clear();
            throw;
        }
        if (m_reset_pending)
            m_added.clear();
    }

    void record_added(std::span<const std::byte> edge)
    {
        const auto from = edge.first(m_edges.target_offset());
        const auto to = edge.subspan(m_edges.target_offset());
        const auto stored = m_tree.find(from);
        if (!m_restart_backward)
        {
            const auto to_bound = m_backward.bound(m_tree.find(to));
            m_restart_backward = to_bound != infinity && m_backward.bound(stored) > to_bound + 1;
        }
        if (stored)
            m_added.add(from, to);
    }

    template<RelationViewConcept A, RelationViewConcept R>
    void seed(size_t slot, const A& added, const R& removed)
    {
        if (!m_reset_pending && !m_edges.evaluator().seed(slot, added, removed, [&](std::span<const std::byte> edge) { record_added(edge); }))
            m_reset_pending = true;
    }

    void reset(const H& heuristic)
    {
        m_tree.clear();
        m_reset_pending = false;
        restart_backward(heuristic);
    }

    void restart_backward(const H& heuristic) { m_backward.start(m_targets.ensure_in(m_tree, heuristic)); }
};

}  // namespace ygg::database::incremental

#endif
