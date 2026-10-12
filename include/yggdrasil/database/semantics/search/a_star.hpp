/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_SEARCH_A_STAR_HPP_
#define YGG_DATABASE_SEMANTICS_SEARCH_A_STAR_HPP_

#include "yggdrasil/containers/raw_array_set.hpp"
#include "yggdrasil/database/semantics/search/search_tree.hpp"
#include "yggdrasil/database/semantics/search/backward_frontier.hpp"
#include "yggdrasil/database/semantics/search/concepts.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <optional>
#include <ranges>
#include <span>
#include <vector>

namespace ygg::database
{

/// Counters of one A* search.
struct AStarCounters
{
    size_t expanded = 0;            ///< Vertices whose successors were generated.
    size_t tree_hits = 0;           ///< Searches ended on a stored path.
    size_t backward_vertices = 0;   ///< Vertices the backward frontier found.
    size_t backward_exhausted = 0;  ///< Searches whose exhausted backward frontier proved the sources unreachable.

    AStarCounters& operator+=(const AStarCounters& other) noexcept
    {
        expanded += other.expanded;
        tree_hits += other.tree_hits;
        backward_vertices += other.backward_vertices;
        backward_exhausted += other.backward_exhausted;
        return *this;
    }
};

/// One best-first search from sources to the targets with unit edge costs (Hart, Nilsson & Raphael, "A Formal Basis
/// for the Heuristic Determination of Minimum Cost Paths", IEEE Trans. SSC 1968). The estimate is the maximum of the
/// caller's heuristic h, the tree's learned distance and the backward frontier's bound; all three are consistent, so a
/// vertex is closed once. Buckets by f, then by g: within an f, larger g is expanded first, which leaves plateaus
/// towards the targets.
///
/// The search is bidirectional: before each expansion the backward frontier advances while it is not larger than the
/// forward open list (Pohl's cardinality comparison, "Bi-directional Search", Machine Intelligence 6, 1971). It ends at
/// a target, at a stored vertex on a valid tree path (MPGAA*'s goal condition), or when the exhausted backward frontier
/// excludes every source.
///
/// Learning (GAA*: h(s) := m - g(s) for closed s) uses m = min(d, the smallest g + h_learn of an open or dropped
/// vertex) instead of the distance d, where h_learn = max(h, learned) omits the backward bound: a search that ends
/// early on a backward level, or on an exhausted frontier, then still learns bounds that are consistent without the
/// backward frontier, which is discarded when edges are added. [Our addition.]
template<typename Oracle, DistanceHeuristic H>
    requires SuccessorOracle<Oracle> && PredecessorOracle<Oracle> && EdgeCheck<Oracle>
class AStar
{
public:
    AStar(Oracle& oracle,
          SearchTree& tree,
          BackwardFrontier& backward,
          const H& heuristic,
          const RawArraySet<std::byte, 64>& targets,
          size_t vertex_size,
          uint32_t version) :
        m_oracle(oracle),
        m_tree(tree),
        m_backward(backward),
        m_heuristic(heuristic),
        m_targets(targets),
        m_version(version),
        m_vertices(vertex_size)
    {
    }
    AStar(const AStar&) = delete;
    AStar& operator=(const AStar&) = delete;

    /// The distance from the nearest source; `sources` has size() and row(i).
    template<typename Sources>
    uint_t run(const Sources& sources)
    {
        m_tree.begin_search();
        if (closed_off(sources))
        {
            ++m_counters.backward_exhausted;
            return infinity;
        }
        if (const auto distance = reuse(sources))
            return *distance;
        for (size_t i = 0; i < sources.size(); ++i)
            push(sources.row(i), 0, none);
        for (uint_t f = 0; f < m_buckets.size(); ++f)
            while (const auto g = top_bucket(f))
            {
                if (advance_backward(sources))
                {
                    ++m_counters.backward_exhausted;
                    return finish(infinity, std::nullopt);
                }
                const auto index = m_buckets[f][*g].back();
                m_buckets[f][*g].pop_back();
                if (const auto distance = visit(index, f))
                    return *distance;
            }
        return finish(infinity, std::nullopt);
    }

    const AStarCounters& counters() const noexcept { return m_counters; }

private:
    Oracle& m_oracle;
    SearchTree& m_tree;
    BackwardFrontier& m_backward;
    const H& m_heuristic;
    const RawArraySet<std::byte, 64>& m_targets;
    uint32_t m_version;

    RawArraySet<std::byte, 64> m_vertices;  ///< Generated vertices, indexed like the vectors below.
    std::vector<uint_t> m_g;
    std::vector<uint_t> m_h;          ///< The A* estimate when queued.
    std::vector<uint_t> m_h_learn;    ///< max(h, learned): the estimate learning may rely on.
    std::vector<uint_t> m_parent;
    std::vector<bool> m_closed;
    size_t m_open = 0;
    uint_t m_dropped = infinity;  ///< The smallest g + h_learn among vertices dropped by the backward bound alone.
    std::vector<std::vector<std::vector<uint_t>>> m_buckets;
    AStarCounters m_counters;

    /// The larger lower bound of a stored vertex; exact once its next chain is valid.
    uint_t value(uint_t id) const noexcept
    {
        const auto level = m_backward.level(id);
        return level == none ? m_tree.learned(id) : std::max(m_tree.learned(id), level);
    }

    bool on_valid_path(uint_t id)
    {
        return m_tree.on_valid_path(
            id,
            [&](uint_t u) { return value(u); },
            [&](uint_t u) { return m_targets.contains(m_tree.vertex(u)); },
            m_oracle,
            m_version);
    }

    /// Whether the exhausted backward frontier found no source.
    template<typename Sources>
    bool closed_off(const Sources& sources) const
    {
        return m_backward.exhausted()
               && std::ranges::none_of(std::views::iota(size_t(0), sources.size()),
                                       [&](size_t i) { return m_backward.level(m_tree.find(sources.row(i)).value_or(none)) != none; });
    }

    /// A single stored source on a valid tree path has an exact value; a learned infinity proves it dead.
    template<typename Sources>
    std::optional<uint_t> reuse(const Sources& sources)
    {
        if (sources.size() != 1)
            return std::nullopt;
        const auto stored = m_tree.find(sources.row(0));
        if (!stored)
            return std::nullopt;
        if (m_tree.learned(*stored) == infinity)
            return infinity;
        if (!on_valid_path(*stored))
            return std::nullopt;
        ++m_counters.tree_hits;
        return value(*stored);
    }

    /// Cardinality comparison; returns whether the exhausted backward frontier excludes every source.
    template<typename Sources>
    bool advance_backward(const Sources& sources)
    {
        while (!m_backward.exhausted() && m_backward.frontier_size() <= m_open)
        {
            m_counters.backward_vertices += m_backward.step(m_tree, m_oracle, m_heuristic);
            if (closed_off(sources))
                return true;
        }
        return false;
    }

    std::optional<size_t> top_bucket(uint_t f) const
    {
        for (auto g = m_buckets[f].size(); g > 0; --g)
            if (!m_buckets[f][g - 1].empty())
                return g - 1;
        return std::nullopt;
    }

    void enqueue(uint_t index)
    {
        const auto f = m_g[index] + m_h[index];
        if (m_buckets.size() <= f)
            m_buckets.resize(f + 1);
        if (m_buckets[f].size() <= m_g[index])
            m_buckets[f].resize(m_g[index] + 1);
        m_buckets[f][m_g[index]].push_back(index);
    }

    void push(std::span<const std::byte> vertex, uint_t cost, uint_t from)
    {
        const auto found = m_vertices.find(vertex);
        if (found && (m_closed[*found] || m_g[*found] <= cost))
            return;
        if (!found)
        {
            auto estimate = m_heuristic.estimate(vertex);
            assert(from == none || estimate == infinity || m_heuristic.estimate(m_vertices[from]) <= estimate + 1);  // Consistency.
            const auto stored = m_tree.find(vertex);
            if (stored)
                estimate = std::max(estimate, m_tree.learned(*stored));
            if (estimate == infinity)
                return;
            const auto bound = m_backward.bound(stored);
            if (bound == infinity)
            {
                m_dropped = std::min(m_dropped, cost + estimate);
                return;
            }
            m_vertices.insert(vertex);
            m_g.push_back(cost);
            m_h.push_back(std::max(estimate, bound));
            m_h_learn.push_back(estimate);
            m_parent.push_back(from);
            m_closed.push_back(false);
            ++m_open;
        }
        const auto index = found ? *found : uint_t(m_g.size() - 1);
        m_g[index] = cost;
        m_parent[index] = from;
        enqueue(index);
    }

    /// Pops a vertex queued at f; returns the distance if the search ends there.
    std::optional<uint_t> visit(uint_t index, uint_t f)
    {
        if (m_closed[index] || m_g[index] + m_h[index] != f)
            return std::nullopt;
        const auto vertex = m_vertices[index];
        if (m_targets.contains(vertex))
            return finish(f, index);
        const auto stored = m_tree.find(vertex);
        // The backward frontier may have raised its bound since the vertex was queued.
        const auto bound = m_backward.bound(stored);
        if (bound == infinity)
        {
            m_dropped = std::min(m_dropped, m_g[index] + m_h_learn[index]);
            m_closed[index] = true;
            m_h_learn[index] = infinity;
            --m_open;
            return std::nullopt;
        }
        if (bound > m_h[index])
        {
            m_h[index] = bound;
            enqueue(index);
            return std::nullopt;
        }
        // It stays open for learning: its successors were not generated.
        if (stored && on_valid_path(*stored))
        {
            ++m_counters.tree_hits;
            return finish(m_g[index] + value(*stored), index);
        }
        expand(index);
        return std::nullopt;
    }

    void expand(uint_t index)
    {
        m_closed[index] = true;
        --m_open;
        ++m_counters.expanded;
        const auto source = std::vector<std::byte>(m_vertices[index].begin(), m_vertices[index].end());
        m_oracle.successors(source,
                            [&](std::span<const std::byte> to)
                            {
                                push(to, m_g[index] + 1, index);
                                return true;
                            });
    }

    /// Learns for closed vertices (see the class comment) and links the found path into the tree.
    uint_t finish(uint_t distance, std::optional<uint_t> last)
    {
        auto m = std::min(distance, m_dropped);
        for (uint_t i = 0; i < m_closed.size(); ++i)
            if (!m_closed[i] && m_h_learn[i] != infinity)
                m = std::min(m, m_g[i] + m_h_learn[i]);
        // Closed vertices are stored even when they learn nothing: repair walks stored predecessors only.
        for (uint_t i = 0; i < m_closed.size(); ++i)
            if (m_closed[i])
            {
                const auto id = ensure(i);
                if (m > m_g[i])
                    m_tree.learn(id, m == infinity ? infinity : m - m_g[i]);
            }
        if (last)
            for (auto child = *last; m_parent[child] != none; child = m_parent[child])
                m_tree.link(ensure(m_parent[child]), ensure(child));
        return distance;
    }

    uint_t ensure(uint_t index) { return m_tree.ensure(m_vertices[index], m_heuristic.estimate(m_vertices[index])); }
};

}  // namespace ygg::database

#endif
