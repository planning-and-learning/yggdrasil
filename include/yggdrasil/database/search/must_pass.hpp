/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEARCH_MUST_PASS_HPP_
#define YGG_DATABASE_SEARCH_MUST_PASS_HPP_

#include "yggdrasil/containers/raw_array_set.hpp"
#include "yggdrasil/core/config.hpp"
#include "yggdrasil/database/evaluation/bound.hpp"
#include "yggdrasil/database/operators/relational/project.hpp"
#include "yggdrasil/database/query/query.hpp"
#include "yggdrasil/database/relation/relation_view.hpp"
#include "yggdrasil/database/search/a_star.hpp"
#include "yggdrasil/database/search/backward_frontier.hpp"
#include "yggdrasil/database/search/concepts.hpp"
#include "yggdrasil/database/search/detail/min_distance.hpp"
#include "yggdrasil/database/search/must_pass_plan.hpp"
#include "yggdrasil/database/search/search_tree.hpp"

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

namespace ygg::database
{

/// What the explorations of all calls visited, with the counters of their searches (AStarCounters).
struct MustPassStatistics : AStarCounters
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
///     and a vertex bound, so E_k is explored on demand from S_k and never materialized or kept.
///  2. Witness. A* (search/a_star.hpp, as in MinDistance) finds a shortest path from S_k to T_k. Only objects on this
///     path can be must-pass: any other object is avoided by it. A short witness passes few objects, so few need
///     verification.
///  3. Verification. An object contained in every source or every target is passed trivially, as initial facts and
///     goals are landmarks. For each other probe object on the witness, the same A* tests whether T_k is reachable in
///     the key's graph without the vertices containing it.
///  4. Bidirectionality. As in MinDistance, every search also runs breadth-first backward from T_k (BackwardFrontier),
///     interleaved by Pohl's cardinality comparison; once that side is exhausted without meeting a source, T_k is
///     unreachable ("Bi-directional Search", Machine Intelligence 6, 1971). Unreachability, the expensive case, is so
///     proven from the smaller side. The heuristic stays consistent on the remaining graph, so the searches are
///     goal-directed; only an object that must be passed costs an exhaustive search. Each search starts with an empty
///     search tree: distances learned without one object do not bound those without another.
///
/// The heuristic H estimates the distance to the key's targets from the bytes of a key followed by a vertex (the bound
/// columns of a successor query); it must be consistent (DistanceHeuristic) for every key, and infinity prunes a
/// vertex. If it has exclude(key, object), it is told before each search which object the search avoids (an empty
/// span: none), so it may estimate on the smaller graph; an abstraction in which the targets become unreachable then
/// proves a must-pass object without a search.
///
/// An edge query that projects vertices onto fewer columns defines an abstraction: every path of the original graph
/// maps to a path of the projection, so objects that must be passed in the projection must be passed by the original's
/// paths too.
///
/// Nothing but compiled plans and static inputs is kept across calls. Inputs are borrowed during must_pass() only.
template<DistanceHeuristic H = BlindDistanceHeuristic>
class MustPass
{
public:
    /// Edges given by a query over inputs with the plan's edge columns; `static_slots` marks its inputs that never
    /// change, which are then read on the first call only.
    MustPass(QueryView<> edges, const MustPassPlan& plan, const std::vector<bool>& static_slots = {}) :
        m_plan(plan),
        m_probe_positions(plan.probe_positions()),
        m_edges(bound::QueryEvaluator(checked(edges, plan), static_slots), plan.edge_columns().span(), plan.keys(), plan.arity()),
        m_keys(std::max<size_t>(plan.key_size(), 1)),
        m_probes(plan.probe_size()),
        m_starts(plan.vertex_size()),
        m_ends(plan.vertex_size()),
        m_tree(plan.vertex_size()),
        m_targets(plan.vertex_size()),
        m_search(plan.vertex_size()),
        m_estimated(plan.key_size() + plan.vertex_size())
    {
    }

    [[nodiscard]] size_t input_count() const noexcept { return m_edges.evaluator().input_count(); }
    [[nodiscard]] const MustPassPlan& plan() const noexcept { return m_plan; }

    /// Appends to `out`, which has the plan's output columns, the must-pass probe objects of every key of the sources and
    /// targets over the edges of the inputs: input(slot) returns slot's relation view.
    template<SlotRelations Input, RelationViewConcept S, RelationViewConcept T, RelationViewConcept P>
    void must_pass(const Input& input, const S& sources, const T& targets, const P& probes, Builder<Relation>& out, H& heuristic)
    {
        m_heuristic = &heuristic;
        detail::require_plan_columns(sources.columns().span(), m_plan.source_columns().span());
        detail::require_plan_columns(targets.columns().span(), m_plan.target_columns().span());
        detail::require_plan_columns(probes.columns().span(), m_plan.probe_columns().span());
        detail::require_plan_columns(out.columns().span(), m_plan.output_columns().span());
        m_edges.evaluator().bind(input);
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

    template<SlotRelations Input, RelationViewConcept S, RelationViewConcept T, RelationViewConcept P>
        requires std::default_initializable<H>
    void must_pass(const Input& input, const S& sources, const T& targets, const P& probes, Builder<Relation>& out)
    {
        auto heuristic = H {};
        must_pass(input, sources, targets, probes, out, heuristic);
    }

    [[nodiscard]] const MustPassStatistics& statistics() const noexcept { return m_statistics; }

private:
    /// Vertices of the current key by id, as a VertexRange.
    struct Vertices
    {
        const VertexSet* set = nullptr;
        std::span<const uint_t> ids;

        [[nodiscard]] size_t size() const noexcept { return ids.size(); }
        [[nodiscard]] std::span<const std::byte> row(size_t i) const { return (*set)[ids[i]]; }
    };

    using Edges = detail::EdgeQuery<bound::QueryEvaluator>;

    /// The current key's edges (Edges::Keyed, evaluated on demand as in MinDistance) without the vertices containing
    /// the excluded probe object: the oracle of AStar and BackwardFrontier.
    class Graph
    {
    public:
        Graph(typename Edges::Keyed edges, MustPass& owner) : m_edges(edges), m_owner(&owner) {}

        template<RowPredicate OnVertex>
        void successors(std::span<const std::byte> vertex, const OnVertex& on_vertex)
        {
            ++m_owner->m_statistics.vertices;
            m_edges.successors(vertex, allowed_only(on_vertex));
        }
        template<RowPredicate OnVertex>
        void predecessors(std::span<const std::byte> vertex, const OnVertex& on_vertex)
        {
            ++m_owner->m_statistics.backward;
            m_edges.predecessors(vertex, allowed_only(on_vertex));
        }
        bool edge_exists(std::span<const std::byte> from, std::span<const std::byte> to)
        { return m_owner->allowed(from) && m_owner->allowed(to) && m_edges.edge_exists(from, to); }

    private:
        /// on_vertex for allowed vertices; the others are skipped.
        template<RowPredicate OnVertex>
        auto allowed_only(const OnVertex& on_vertex)
        {
            return [this, &on_vertex](std::span<const std::byte> next)
            {
                ++m_owner->m_statistics.edges;
                return !m_owner->allowed(next) || on_vertex(next);
            };
        }

        typename Edges::Keyed m_edges;
        MustPass* m_owner;
    };

    /// The heuristic on a vertex of the current key: H estimates from the key followed by the vertex.
    class KeyedHeuristic
    {
    public:
        explicit KeyedHeuristic(MustPass& owner) : m_owner(&owner) {}

        [[nodiscard]] uint_t estimate(std::span<const std::byte> vertex) const { return m_owner->estimate(vertex); }

    private:
        MustPass* m_owner;
    };

    static QueryView<> checked(QueryView<> edges, const MustPassPlan& plan)
    {
        detail::require_plan_columns(edges.columns(), plan.edge_columns().span());
        return edges;
    }

    [[nodiscard]] std::span<const std::byte> key_of(std::span<const std::byte> row) const noexcept { return row.first(m_plan.key_size()); }
    [[nodiscard]] std::span<const std::byte> vertex_of(std::span<const std::byte> row) const noexcept
    { return row.subspan(m_plan.key_size(), m_plan.vertex_size()); }

    /// The row positions of the sources and targets of each key; keys are numbered by their first source.
    template<RelationViewConcept S, RelationViewConcept T>
    void group(const S& sources, const T& targets)
    {
        const auto keyed = m_plan.key_size() > 0;
        m_keys.clear();
        if (keyed)
            for (size_t r = 0; r < sources.size(); ++r)
                m_keys.insert(key_of(sources.row(r)));
        const auto keys = keyed ? m_keys.size() : static_cast<size_t>(1);
        m_key_sources.resize(keys);
        m_key_targets.resize(keys);
        for (size_t key = 0; key < keys; ++key)
        {
            m_key_sources[key].clear();
            m_key_targets[key].clear();
        }
        for (size_t r = 0; r < sources.size(); ++r)
        {
            if (!keyed)
            {
                m_key_sources[0].push_back(r);
                continue;
            }
            const auto key = m_keys.find(key_of(sources.row(r)));
            YGG_ASSUME(key.has_value());  // every source's key was inserted above
            m_key_sources[*key].push_back(r);
        }
        for (size_t r = 0; r < targets.size(); ++r)
        {
            if (!keyed)
                m_key_targets[0].push_back(r);
            else if (const auto key = m_keys.find(key_of(targets.row(r))))  // A key without sources has no paths.
                m_key_targets[*key].push_back(r);
        }
    }

    /// Calls on_probe with each probe object a vertex contains (an object at several positions once per position).
    template<typename OnProbe>
    void probes_of(std::span<const std::byte> vertex, const OnProbe& on_probe) const
    {
        for (const auto position : m_probe_positions)
            if (const auto probe = m_probes.find(vertex.subspan(position, m_plan.probe_size())))
                on_probe(static_cast<uint_t>(*probe));
    }
    [[nodiscard]] bool contains(std::span<const std::byte> vertex, uint_t probe) const
    {
        const auto object = m_probes[probe];
        return std::ranges::any_of(m_probe_positions,
                                   [&](size_t position) { return std::ranges::equal(vertex.subspan(position, m_plan.probe_size()), object); });
    }
    /// Whether the current search may visit a vertex: it does not contain the excluded probe object.
    [[nodiscard]] bool allowed(std::span<const std::byte> vertex) const { return m_excluded == none || !contains(vertex, m_excluded); }

    /// The heuristic's estimate of a vertex of the current key.
    uint_t estimate(std::span<const std::byte> vertex)
    {
        std::ranges::copy(vertex, m_estimated.begin() + static_cast<std::ptrdiff_t>(m_plan.key_size()));
        return m_heuristic->estimate(m_estimated);
    }

    /// Whether a target of the current key is reachable without the vertices containing `excluded` (none: any). The
    /// found path's vertices are passed to on_path.
    template<typename OnPath>
    bool search(uint_t excluded, const OnPath& on_path)
    {
        m_excluded = excluded;
        if constexpr (requires(H& h, std::span<const std::byte> bytes) { h.exclude(bytes, bytes); })
        {
            m_heuristic->exclude(m_key, excluded == none ? std::span<const std::byte>() : std::span<const std::byte>(m_probes[excluded]));
        }
        const auto keep_allowed = [&](const VertexSet& vertices, std::vector<uint_t>& out)
        {
            out.clear();
            for (uint_t id = 0; id < vertices.size(); ++id)
                if (allowed(vertices[id]))
                    out.push_back(id);
        };
        keep_allowed(m_starts, m_allowed_starts);
        keep_allowed(m_ends, m_allowed_ends);
        auto graph = Graph(m_edges.with_key(m_key), *this);
        const auto heuristic = KeyedHeuristic(*this);
        m_tree.clear();
        m_targets.assign(Vertices { &m_ends, m_allowed_ends });
        m_frontier.start(m_tree, m_targets.ensure_in(m_tree, heuristic));
        auto search = AStar<Graph, KeyedHeuristic>(graph, m_tree, m_frontier, heuristic, m_targets.vertices(), m_search, m_edges.evaluator().version());
        const auto reachable = search.run(Vertices { &m_starts, m_allowed_starts }) != infinity;
        m_statistics += search.counters();
        if (reachable)
            search.path(on_path);
        return reachable;
    }

    template<RelationViewConcept S, RelationViewConcept T>
    void verify(const S& sources, const T& targets, uint_t key, Builder<Relation>& out)
    {
        const auto key_bytes = key_of(sources.row(m_key_sources[key].front()));
        m_key = key_bytes;
        std::ranges::copy(key_bytes, m_estimated.begin());
        m_starts.clear();
        for (const auto r : m_key_sources[key])
            m_starts.insert(vertex_of(sources.row(r)));
        m_ends.clear();
        for (const auto r : m_key_targets[key])
            m_ends.insert(vertex_of(targets.row(r)));
        m_candidates.clear();
        const auto on_witness = [&](std::span<const std::byte> vertex)
        {
            probes_of(vertex,
                      [&](uint_t probe)
                      {
                          if (std::ranges::find(m_candidates, probe) == m_candidates.end())
                              m_candidates.push_back(probe);
                      });
        };
        if (!search(none, on_witness))
            return;
        ++m_statistics.keys;
        m_row.assign(key_bytes.begin(), key_bytes.end());
        m_row.resize(m_plan.key_size() + m_plan.probe_size());
        const auto in_all = [&](const VertexSet& vertices, uint_t probe)
        {
            return std::ranges::all_of(std::views::iota(static_cast<uint_t>(0), static_cast<uint_t>(vertices.size())),
                                       [&](uint_t id) { return contains(vertices[id], probe); });
        };
        for (const auto probe : m_candidates)
            // An object in every source or every target is passed trivially, as initial facts and goals are landmarks.
            if (in_all(m_starts, probe) || in_all(m_ends, probe) || (++m_statistics.removals, !search(probe, [](std::span<const std::byte>) {})))
            {
                std::ranges::copy(m_probes[probe], m_row.begin() + static_cast<std::ptrdiff_t>(m_plan.key_size()));
                out.insert(std::span<const std::byte>(m_row));
            }
    }

    MustPassPlan m_plan;
    std::vector<size_t> m_probe_positions;
    Edges m_edges;
    RawArraySet<std::byte> m_keys;    ///< Interned key bytes; unused without key columns.
    RawArraySet<std::byte> m_probes;  ///< Interned probe objects.
    std::vector<std::vector<size_t>> m_key_sources;
    std::vector<std::vector<size_t>> m_key_targets;
    // The current key.
    std::span<const std::byte> m_key;  ///< Borrowed from the sources during must_pass().
    VertexSet m_starts;
    VertexSet m_ends;
    // Searches.
    H* m_heuristic = nullptr;
    uint_t m_excluded = none;  ///< The probe object whose vertices the current search avoids, or none.
    std::vector<uint_t> m_allowed_starts;
    std::vector<uint_t> m_allowed_ends;
    SearchTree m_tree;
    BackwardFrontier m_frontier;
    detail::TargetSet m_targets;
    AStarStorage m_search;
    std::vector<uint_t> m_candidates;    ///< Probe objects on the witness.
    std::vector<std::byte> m_estimated;  ///< The key and a vertex, as given to the heuristic.
    std::vector<std::byte> m_row;
    MustPassStatistics m_statistics;
};

}  // namespace ygg::database

#endif
