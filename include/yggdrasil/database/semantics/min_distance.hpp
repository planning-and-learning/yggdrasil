/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_MIN_DISTANCE_HPP_
#define YGG_DATABASE_SEMANTICS_MIN_DISTANCE_HPP_

#include "yggdrasil/database/semantics/bound/query_evaluation.hpp"
#include "yggdrasil/database/semantics/details/min_distance.hpp"
#include "yggdrasil/database/semantics/distance.hpp"
#include "yggdrasil/database/semantics/search/a_star.hpp"
#include "yggdrasil/database/semantics/search/backward_frontier.hpp"
#include "yggdrasil/database/semantics/search/concepts.hpp"
#include "yggdrasil/database/semantics/search/search_tree.hpp"

#include <cstddef>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace ygg::database
{

/// The counters of all searches (AStarCounters) and of the repairs between them.
struct MinDistanceStatistics : AStarCounters
{
    size_t repaired = 0;  ///< Vertices whose learned distance dropped after added edges.
};

/// Exact minimum distance from any source to any target, or uint_t's maximum if none is reachable, from the inputs of
/// one call. Edges are given by a query over inputs, evaluated on demand and never materialized, or as a relation,
/// which is a query of one input.
///
/// Method:
///  1. Edges. The edge query is translated into union normal form (bound/union_normal_form.hpp), and a
///     bound::QueryEvaluator (bound/query_evaluation.hpp) evaluates it over the call's inputs with some columns bound:
///     a vertex's source columns for its successors, its target columns for its predecessors, both for an edge check.
///  2. Search. A* (search/a_star.hpp) with f/g buckets. The estimate is the maximum of the heuristic H, the distances
///     the search learned, and the backward frontier's bound.
///  3. Bidirectionality. The backward frontier (search/backward_frontier.hpp) advances whenever it is not larger than
///     the forward open list. Its levels are exact distances; once it is exhausted, sources outside it are
///     unreachable. A search ends at a vertex on a valid chain to a target (search/search_tree.hpp).
///
/// Nothing but compiled plans and static inputs is kept across calls; incremental::MinDistance keeps the search tree
/// and updates it from input changes. Inputs are borrowed during min_distance() only.
template<ColumnTypes Values, DistanceHeuristic H = BlindDistanceHeuristic>
    requires ColumnValueFor<uint_t, Values>
class MinDistance
{
public:
    /// Edges given by a query over inputs. `edges` has the plan's edge columns; `static_slots` marks its inputs that
    /// never change, which are then read on the first call only.
    MinDistance(QueryView<Values> edges, const DistancePlan<Values>& plan, const std::vector<bool>& static_slots = {}) :
        MinDistance(bound::QueryEvaluator<Values>(detail::checked_edges(edges, plan), static_slots), plan)
    {
    }

    /// Edges given as a relation with the plan's edge columns.
    explicit MinDistance(const DistancePlan<Values>& plan, bool static_edges = false) :
        MinDistance(detail::relation_edges<bound::QueryEvaluator<Values>>(plan, static_edges), plan)
    {
    }

    size_t input_count() const noexcept { return m_edges.evaluator().input_count(); }

    /// The minimum distance over the edges of the inputs: input(slot) returns slot's relation view.
    template<SlotRelations<Values> Input, RelationViewConcept<Values> S, RelationViewConcept<Values> T>
    uint_t min_distance(const Input& input, const S& sources, const T& targets, const H& heuristic = {})
    {
        m_edges.require_source_columns(sources);
        m_edges.require_target_columns(targets);
        m_edges.evaluator().bind(input);
        m_tree.clear();
        m_targets.assign(targets);
        m_backward.start(m_targets.ensure_in(m_tree, heuristic));
        auto search = AStar<Values, Edges, H>(m_edges, m_tree, m_backward, heuristic, m_targets.vertices(), m_search, m_edges.evaluator().version());
        const auto distance = search.run(sources);
        m_statistics += search.counters();
        return distance;
    }

    /// The minimum distance over an edge relation (for the relation constructor).
    template<RelationViewConcept<Values> E, RelationViewConcept<Values> S, RelationViewConcept<Values> T>
    uint_t min_distance_edges(const E& edges, const S& sources, const T& targets, const H& heuristic = {})
    {
        return min_distance([&](size_t) -> const E& { return edges; }, sources, targets, heuristic);
    }

    const MinDistanceStatistics& statistics() const noexcept { return m_statistics; }

private:
    using Edges = detail::EdgeQuery<bound::QueryEvaluator<Values>, Values>;

    MinDistance(bound::QueryEvaluator<Values> edges, const DistancePlan<Values>& plan) :
        m_edges(std::move(edges), plan),
        m_tree(plan.tuple_size()),
        m_targets(plan.tuple_size()),
        m_search(plan.tuple_size())
    {
    }

    Edges m_edges;
    SearchTree m_tree;
    BackwardFrontier m_backward;
    detail::TargetSet<Values> m_targets;
    AStarStorage m_search;
    MinDistanceStatistics m_statistics;
};

}  // namespace ygg::database

#endif
