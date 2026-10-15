/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_MUST_PASS_HPP_
#define YGG_DATABASE_SEMANTICS_MUST_PASS_HPP_

#include "yggdrasil/containers/raw_array_set.hpp"
#include "yggdrasil/database/semantics/bound/query_evaluation.hpp"
#include "yggdrasil/database/semantics/details/min_distance.hpp"
#include "yggdrasil/database/semantics/must_pass_plan.hpp"
#include "yggdrasil/database/semantics/operations.hpp"
#include "yggdrasil/database/semantics/relation_view.hpp"
#include "yggdrasil/database/semantics/search/concepts.hpp"
#include "yggdrasil/database/semantics/search/search_tree.hpp"
#include "yggdrasil/database/syntax/query.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <queue>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace ygg::database
{

/// What the explorations of all calls visited.
struct MustPassStatistics
{
    size_t keys = 0;      ///< Keys whose targets were reachable.
    size_t vertices = 0;  ///< Expanded vertices.
    size_t edges = 0;     ///< Explored edges.
    size_t removals = 0;  ///< Reachability tests without a probe object.
    size_t backward = 0;  ///< Vertices expanded backward.
};

/// Must-pass objects: for each key k with sources S_k, targets T_k and edges E_k, the probe objects x such that every
/// path from S_k to T_k passes a vertex containing x, i.e. T_k is unreachable once every vertex containing x is removed.
/// A key whose targets are unreachable has none. These are landmarks of the key's graph (Hoffmann, Porteous &
/// Sebastia, "Ordered Landmarks in Planning", JAIR 2004), verified as there and in domain transition graphs (Richter,
/// Helmert & Westphal, "Landmarks Revisited", AAAI 2008) by testing reachability without the candidate; an object, not
/// a single vertex, is the unit, since a path may pass an object in many different vertices.
///
/// Method, per key:
///  1. Edges. As in MinDistance, a bound::QueryEvaluator evaluates the edge query over the call's inputs with the key
///     and a vertex bound, so E_k is explored on demand from S_k and never materialized; successors are kept for the
///     key's searches.
///  2. Witness. A* (Hart, Nilsson & Raphael 1968) finds a shortest path from S_k to T_k. Only objects on this path can
///     be must-pass: any other object is avoided by it. A short witness passes few objects, so few need verification.
///  3. Verification. An object contained in every source or every target is passed trivially, as initial facts and
///     goals are landmarks. For each other probe object on the witness, A* tests whether T_k is reachable without the
///     vertices containing it.
///  4. Bidirectionality. Every search also runs breadth-first backward from T_k, alternating with the forward side by
///     the vertices each expanded; once it is exhausted without meeting a source, T_k is unreachable (Pohl,
///     "Bi-directional Search", Machine Intelligence 6, 1971). Unreachability, the expensive case, is so proven from
///     the smaller side. The heuristic stays admissible on the remaining graph, so the searches are goal-directed; only
///     an object that must be passed costs an exhaustive search.
///
/// The heuristic H estimates the distance to the key's targets from the bytes of a key followed by a vertex (the bound
/// columns of a successor query); it must be admissible for every key, and infinity prunes a vertex. If it has
/// exclude(key, object), it is told before each search which object the search avoids (an empty span: none), so it may
/// estimate on the smaller graph; an abstraction in which the targets become unreachable then proves a must-pass
/// object without a search.
///
/// An edge query that projects vertices onto fewer columns defines an abstraction: every path of the original graph
/// maps to a path of the projection, so objects that must be passed in the projection must be passed by the original's
/// paths too.
///
/// Nothing but compiled plans and static inputs is kept across calls. Inputs are borrowed during must_pass() only.
template<ColumnTypes Values = DefaultColumnTypes, DistanceHeuristic H = BlindDistanceHeuristic>
class MustPass
{
public:
    /// Edges given by a query over inputs with the plan's edge columns; `static_slots` marks its inputs that never
    /// change, which are then read on the first call only.
    MustPass(QueryView<Values> edges, const MustPassPlan<Values>& plan, const std::vector<bool>& static_slots = {}) :
        m_plan(plan),
        m_probe_positions(plan.probe_positions()),
        m_edges(checked(edges, plan), static_slots),
        m_forward(m_edges.prepare(detail::column_range(0, plan.keys() + plan.arity()))),
        m_backward(m_edges.prepare(key_and_target_positions(plan))),
        m_keys(std::max<size_t>(plan.key_size(), 1)),
        m_probes(plan.probe_size()),
        m_vertices(plan.vertex_size())
    {
    }

    size_t input_count() const noexcept { return m_edges.input_count(); }
    const MustPassPlan<Values>& plan() const noexcept { return m_plan; }

    /// Appends to `out`, which has the plan's output columns, the must-pass probe objects of every key of the sources and
    /// targets over the edges of the inputs: input(slot) returns slot's relation view.
    template<SlotRelations<Values> Input, RelationViewConcept<Values> S, RelationViewConcept<Values> T, RelationViewConcept<Values> P>
    void must_pass(const Input& input, const S& sources, const T& targets, const P& probes, Builder<Relation<Values>>& out, H& heuristic)
    {
        m_heuristic = &heuristic;
        detail::require_plan_columns(sources.columns().span(), m_plan.source_columns().span());
        detail::require_plan_columns(targets.columns().span(), m_plan.target_columns().span());
        detail::require_plan_columns(probes.columns().span(), m_plan.probe_columns().span());
        detail::require_plan_columns(out.columns().span(), m_plan.output_columns().span());
        m_edges.bind(input);
        m_probes.clear();
        for (size_t r = 0; r < probes.size(); ++r)
            m_probes.insert(probes.row(r));
        if (m_probes.empty())
            return;
        group(sources, targets);
        for (uint_t key = 0; key < m_key_targets.size(); ++key)
            if (!m_key_sources[key].empty() && !m_key_targets[key].empty())
                verify(sources, targets, key, out);
    }

    template<SlotRelations<Values> Input, RelationViewConcept<Values> S, RelationViewConcept<Values> T, RelationViewConcept<Values> P>
        requires std::default_initializable<H>
    void must_pass(const Input& input, const S& sources, const T& targets, const P& probes, Builder<Relation<Values>>& out)
    {
        auto heuristic = H {};
        must_pass(input, sources, targets, probes, out, heuristic);
    }

    const MustPassStatistics& statistics() const noexcept { return m_statistics; }

private:
    static constexpr auto none = std::numeric_limits<uint_t>::max();

    /// The key columns and the target vertex of an edge, the bound columns of a predecessor query.
    static std::vector<size_t> key_and_target_positions(const MustPassPlan<Values>& plan)
    {
        auto positions = detail::column_range(0, plan.keys());
        for (const auto position : detail::column_range(plan.keys() + plan.arity(), plan.keys() + 2 * plan.arity()))
            positions.push_back(position);
        return positions;
    }

    static QueryView<Values> checked(QueryView<Values> edges, const MustPassPlan<Values>& plan)
    {
        detail::require_plan_columns(edges.columns(), plan.edge_columns().span());
        return edges;
    }

    std::span<const std::byte> key_of(std::span<const std::byte> row) const noexcept { return row.first(m_plan.key_size()); }
    std::span<const std::byte> vertex_of(std::span<const std::byte> row) const noexcept { return row.subspan(m_plan.key_size(), m_plan.vertex_size()); }

    /// The row positions of the sources and targets of each key; keys are numbered by their first source.
    template<RelationViewConcept<Values> S, RelationViewConcept<Values> T>
    void group(const S& sources, const T& targets)
    {
        const auto keyed = m_plan.key_size() > 0;
        m_keys.clear();
        if (keyed)
            for (size_t r = 0; r < sources.size(); ++r)
                m_keys.insert(key_of(sources.row(r)));
        const auto keys = keyed ? m_keys.size() : size_t(1);
        m_key_sources.resize(keys);
        m_key_targets.resize(keys);
        for (size_t key = 0; key < keys; ++key)
        {
            m_key_sources[key].clear();
            m_key_targets[key].clear();
        }
        for (size_t r = 0; r < sources.size(); ++r)
            m_key_sources[keyed ? *m_keys.find(key_of(sources.row(r))) : 0].push_back(r);
        for (size_t r = 0; r < targets.size(); ++r)
        {
            if (!keyed)
                m_key_targets[0].push_back(r);
            else if (const auto key = m_keys.find(key_of(targets.row(r))))  // A key without sources has no paths.
                m_key_targets[*key].push_back(r);
        }
    }

    /// Interns a vertex of the current key with the probe objects it contains.
    uint_t intern(std::span<const std::byte> vertex)
    {
        const auto before = m_vertices.size();
        const auto id = uint_t(m_vertices.insert(vertex));
        if (m_vertices.size() == before)
            return id;
        m_probe_offsets.push_back(m_vertex_probes.size());
        for (const auto position : m_probe_positions)
            if (const auto probe = m_probes.find(vertex.subspan(position, m_plan.probe_size())))
                if (std::ranges::find(m_vertex_probes.begin() + long(m_probe_offsets.back()), m_vertex_probes.end(), uint_t(*probe)) == m_vertex_probes.end())
                    m_vertex_probes.push_back(uint_t(*probe));
        m_successor_ranges.emplace_back(0, none);
        m_is_target.push_back(false);
        return id;
    }
    std::span<const uint_t> probes_of(uint_t vertex) const
    {
        const auto end = vertex + 1 < m_probe_offsets.size() ? m_probe_offsets[vertex + 1] : m_vertex_probes.size();
        return std::span<const uint_t>(m_vertex_probes).subspan(m_probe_offsets[vertex], end - m_probe_offsets[vertex]);
    }
    /// The successors of a vertex, evaluated on first use.
    std::span<const uint_t> successors(uint_t vertex)
    {
        if (m_successor_ranges[vertex].second == none)
        {
            std::ranges::copy(m_vertices[vertex], m_bound.begin() + long(m_plan.key_size()));
            m_expanded.clear();
            m_edges.evaluate(m_forward,
                             m_bound,
                             [&](std::span<const std::byte> edge)
                             {
                                 m_expanded.push_back(intern(edge.subspan(m_plan.edge_target_offset(), m_plan.vertex_size())));
                                 return true;
                             });
            m_successor_ranges[vertex] = { m_successors.size(), uint_t(m_expanded.size()) };
            m_successors.insert(m_successors.end(), m_expanded.begin(), m_expanded.end());
            ++m_statistics.vertices;
            m_statistics.edges += m_expanded.size();
        }
        const auto [first, count] = m_successor_ranges[vertex];
        return std::span<const uint_t>(m_successors).subspan(first, count);
    }

    /// The predecessors of a vertex, evaluated on first use.
    std::span<const uint_t> predecessors(uint_t vertex)
    {
        if (m_predecessor_ranges.size() < m_vertices.size())
            m_predecessor_ranges.resize(m_vertices.size(), { 0, none });
        if (m_predecessor_ranges[vertex].second == none)
        {
            std::ranges::copy(m_vertices[vertex], m_bound.begin() + long(m_plan.key_size()));
            m_expanded.clear();
            m_edges.evaluate(m_backward,
                             m_bound,
                             [&](std::span<const std::byte> edge)
                             {
                                 m_expanded.push_back(intern(edge.subspan(m_plan.key_size(), m_plan.vertex_size())));
                                 return true;
                             });
            if (m_predecessor_ranges.size() < m_vertices.size())
                m_predecessor_ranges.resize(m_vertices.size(), { 0, none });
            m_predecessor_ranges[vertex] = { m_predecessors.size(), uint_t(m_expanded.size()) };
            m_predecessors.insert(m_predecessors.end(), m_expanded.begin(), m_expanded.end());
            ++m_statistics.backward;
        }
        const auto [first, count] = m_predecessor_ranges[vertex];
        return std::span<const uint_t>(m_predecessors).subspan(first, count);
    }

    /// The heuristic's estimate of a vertex of the current key, computed once.
    uint_t estimate(uint_t vertex)
    {
        if (m_estimates.size() < m_vertices.size())
            m_estimates.resize(m_vertices.size(), none);
        if (m_estimates[vertex] == none)
        {
            std::ranges::copy(m_vertices[vertex], m_estimated.begin() + long(m_plan.key_size()));
            m_estimates[vertex] = m_heuristic->estimate(m_estimated);
        }
        return m_estimates[vertex];
    }

    /// A target reached by A* from the key's sources without the vertices containing `excluded` (none: any), or none.
    /// The parents of a search without exclusion form the witness path.
    uint_t search(uint_t excluded)
    {
        const auto allowed = [&](uint_t vertex)
        { return excluded == none || std::ranges::find(probes_of(vertex), excluded) == probes_of(vertex).end(); };
        if constexpr (requires(H& h, std::span<const std::byte> bytes) { h.exclude(bytes, bytes); })
        {
            const auto key = std::span<const std::byte>(m_bound).first(m_plan.key_size());
            m_heuristic->exclude(key, excluded == none ? std::span<const std::byte>() : std::span<const std::byte>(m_probes[excluded]));
            m_estimates.clear();
        }
        ++m_stamp;
        m_open = {};
        const auto reach = [&](uint_t vertex, uint_t g, uint_t parent)
        {
            if (m_stamp_of.size() < m_vertices.size())
            {
                m_stamp_of.resize(m_vertices.size(), 0);
                m_g.resize(m_vertices.size(), none);
                m_parent.resize(m_vertices.size(), none);
            }
            if (!allowed(vertex) || (m_stamp_of[vertex] == m_stamp && m_g[vertex] <= g))
                return;
            const auto h = estimate(vertex);
            if (h == infinity)
                return;
            m_stamp_of[vertex] = m_stamp;
            m_g[vertex] = g;
            m_parent[vertex] = parent;
            m_open.emplace(g + h, infinity - g, vertex);  // Ties go to the deeper vertex.
        };
        for (const auto start : m_starts)
            reach(start, 0, none);
        // Backward breadth-first from the targets: once it is exhausted without meeting a source, no target is
        // reachable (bidirectional search, Pohl 1971). The sides alternate by the vertices they expanded, so an
        // unreachable key is decided by the smaller of the two spaces at about twice its cost, and a reachable one costs
        // at most twice the forward search. Meeting a source decides a removal test; the witness needs the forward
        // path, so its search continues forward.
        m_backward_queue.clear();
        m_backward_next = 0;
        const auto back = [&](uint_t vertex)
        {
            if (m_back_stamp.size() < m_vertices.size())
                m_back_stamp.resize(m_vertices.size(), 0);
            if (!allowed(vertex) || m_back_stamp[vertex] == m_stamp)
                return false;
            m_back_stamp[vertex] = m_stamp;
            m_backward_queue.push_back(vertex);
            return std::ranges::find(m_starts, vertex) != m_starts.end();
        };
        auto met = false;  // Whether the backward side reached a source: a target is reachable.
        auto forward_expanded = size_t(0);
        for (const auto end : m_ends)
            met = back(end) || met;
        if (met && excluded != none)
            return m_ends.front();
        while (!m_open.empty())
        {
            if (!met && m_backward_next == m_backward_queue.size())
                return none;
            if (!met && m_backward_next <= forward_expanded)
            {
                const auto vertex = m_backward_queue[m_backward_next++];
                for (const auto previous : predecessors(vertex))
                    met = back(previous) || met;
                if (met && excluded != none)
                    return m_ends.front();
                continue;
            }
            const auto [f, depth, vertex] = m_open.top();
            m_open.pop();
            const auto g = infinity - depth;
            if (g != m_g[vertex])
                continue;
            ++forward_expanded;
            if (m_is_target[vertex])
                return vertex;
            for (const auto next : successors(vertex))
                reach(next, g + 1, vertex);
        }
        return none;
    }

    template<RelationViewConcept<Values> S, RelationViewConcept<Values> T>
    void verify(const S& sources, const T& targets, uint_t key, Builder<Relation<Values>>& out)
    {
        const auto key_bytes = key_of(sources.row(m_key_sources[key].front()));
        m_vertices.clear();
        m_probe_offsets.clear();
        m_vertex_probes.clear();
        m_successor_ranges.clear();
        m_successors.clear();
        m_predecessor_ranges.clear();
        m_predecessors.clear();
        m_back_stamp.clear();
        m_is_target.clear();
        m_stamp_of.clear();
        m_g.clear();
        m_parent.clear();
        m_estimates.clear();
        m_bound.assign(key_bytes.begin(), key_bytes.end());
        m_bound.resize(m_plan.key_size() + m_plan.vertex_size());
        m_estimated.assign(m_bound.begin(), m_bound.end());
        m_starts.clear();
        for (const auto r : m_key_sources[key])
            m_starts.push_back(intern(vertex_of(sources.row(r))));
        m_ends.clear();
        for (const auto r : m_key_targets[key])
        {
            const auto end = intern(vertex_of(targets.row(r)));
            m_is_target[end] = true;
            m_ends.push_back(end);
        }
        const auto reached = search(none);
        if (reached == none)
            return;
        ++m_statistics.keys;
        m_candidates.clear();
        for (auto vertex = reached; vertex != none; vertex = m_parent[vertex])
            for (const auto probe : probes_of(vertex))
                if (std::ranges::find(m_candidates, probe) == m_candidates.end())
                    m_candidates.push_back(probe);
        m_row.assign(key_bytes.begin(), key_bytes.end());
        m_row.resize(m_plan.key_size() + m_plan.probe_size());
        const auto in_all = [&](const std::vector<uint_t>& vertices, uint_t probe)
        { return std::ranges::all_of(vertices, [&](uint_t vertex) { return std::ranges::find(probes_of(vertex), probe) != probes_of(vertex).end(); }); };
        for (const auto probe : m_candidates)
            // An object in every source or every target is passed trivially, as initial facts and goals are landmarks.
            if (in_all(m_starts, probe) || in_all(m_ends, probe) || (++m_statistics.removals, search(probe) == none))
            {
                std::ranges::copy(m_probes[probe], m_row.begin() + long(m_plan.key_size()));
                out.insert(std::span<const std::byte>(m_row));
            }
    }

    MustPassPlan<Values> m_plan;
    std::vector<size_t> m_probe_positions;
    bound::QueryEvaluator<Values> m_edges;
    size_t m_forward;
    size_t m_backward;
    RawArraySet<std::byte> m_keys;    ///< Interned key bytes; unused without key columns.
    RawArraySet<std::byte> m_probes;  ///< Interned probe objects.
    std::vector<std::vector<size_t>> m_key_sources;
    std::vector<std::vector<size_t>> m_key_targets;
    // The explored graph of the current key.
    VertexSet m_vertices;
    std::vector<size_t> m_probe_offsets;  ///< The probe objects of vertex v start at m_vertex_probes[m_probe_offsets[v]].
    std::vector<uint_t> m_vertex_probes;
    std::vector<std::pair<size_t, uint_t>> m_successor_ranges;  ///< First successor and count; count none if unexpanded.
    std::vector<uint_t> m_successors;
    std::vector<std::pair<size_t, uint_t>> m_predecessor_ranges;  ///< First predecessor and count; count none if unexpanded.
    std::vector<uint_t> m_predecessors;
    std::vector<uint_t> m_expanded;  ///< Successors of the vertex being expanded.
    std::vector<bool> m_is_target;
    std::vector<uint_t> m_starts;
    std::vector<uint_t> m_ends;  ///< The key's target vertices.
    // Searches.
    H* m_heuristic = nullptr;
    std::vector<uint_t> m_estimates;   ///< The heuristic's estimate of each vertex, or none if not computed.
    std::vector<uint32_t> m_stamp_of;  ///< The search that reached a vertex last.
    uint32_t m_stamp = 0;
    std::vector<uint_t> m_g;
    std::vector<uint_t> m_parent;
    std::priority_queue<std::tuple<uint_t, uint_t, uint_t>, std::vector<std::tuple<uint_t, uint_t, uint_t>>, std::greater<>> m_open;  ///< f, infinity - g, vertex.
    std::vector<uint_t> m_candidates;  ///< Probe objects on the witness.
    std::vector<uint32_t> m_back_stamp;  ///< The search whose backward side reached a vertex last.
    std::vector<uint_t> m_backward_queue;
    size_t m_backward_next = 0;
    std::vector<std::byte> m_estimated;  ///< The key and a vertex, as given to the heuristic.
    std::vector<std::byte> m_bound;    ///< The key and a vertex, the bound columns of a successor query.
    std::vector<std::byte> m_row;
    MustPassStatistics m_statistics;
};

}  // namespace ygg::database

#endif
