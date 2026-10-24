/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_SEARCH_BACKWARD_FRONTIER_HPP_
#define YGG_DATABASE_SEMANTICS_SEARCH_BACKWARD_FRONTIER_HPP_

#include "yggdrasil/database/semantics/search/concepts.hpp"
#include "yggdrasil/database/semantics/search/search_tree.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace ygg::database
{

/// A breadth-first search backwards from the targets over the vertices of a SearchTree, grown one level at a time
/// while the forward search runs (the caller interleaves it by Pohl's cardinality comparison). Found vertices get their
/// exact distance as level and a next pointer to their BFS parent. Vertices not yet found are at least depth + 1 away,
/// and once the frontier is exhausted no other vertex can reach a target.
///
/// The found vertices form a perimeter around the targets as in perimeter search (Dillenburg & Nelson, "Perimeter
/// Search", Artificial Intelligence 1994; Manzini, "BIDA*", Artificial Intelligence 1995), but the perimeter grows during
/// the search, and only its depth bound is used outside it, not the front-to-front estimate min over perimeter
/// vertices p of h(v, p) + d(p).
///
/// These bounds form a consistent heuristic of their own. Edges removed after a level keep the levels lower bounds;
/// added edges invalidate them, and the caller restarts the search.
class BackwardFrontier
{
public:
    void start(std::span<const uint_t> targets);

    /// Advances one level; returns the number of vertices found.
    template<PredecessorOracle Predecessors, DistanceHeuristic H>
    size_t step(SearchTree& tree, Predecessors& predecessors, const H& heuristic)
    {
        std::swap(m_frontier, m_expanding);
        m_frontier.clear();
        const auto level = m_depth + 1;
        for (const auto parent : m_expanding)
        {
            // A copy: inserting predecessors into the tree may move its vertices.
            m_vertex.assign(tree.vertex(parent).begin(), tree.vertex(parent).end());
            predecessors.predecessors(m_vertex,
                                      [&](std::span<const std::byte> from)
                                      {
                                          const auto id = tree.ensure(from, heuristic);
                                          auto& found = level_slot(id);
                                          if (found != none)
                                              return true;
                                          found = level;
                                          tree.link(id, parent);
                                          m_frontier.push_back(id);
                                          return true;
                                      });
        }
        m_depth = level;
        m_exhausted = m_frontier.empty();
        return m_frontier.size();
    }

    bool exhausted() const noexcept { return m_exhausted; }
    size_t frontier_size() const noexcept { return m_frontier.size(); }
    /// The distance of a found vertex, or none: exact on the edges of the search that found it, a lower bound after
    /// edges are removed.
    uint_t level(uint_t id) const noexcept { return id < m_level.size() ? m_level[id] : none; }
    /// A lower bound on any vertex's distance: its level if found, otherwise one beyond the last level, or infinity
    /// once the frontier is exhausted.
    uint_t bound(std::optional<uint_t> id) const noexcept
    {
        if (id && level(*id) != none)
            return level(*id);
        return m_exhausted ? infinity : m_depth + 1;
    }

private:
    std::vector<uint_t> m_level;  ///< By the tree's vertex index.
    std::vector<uint_t> m_frontier;
    std::vector<uint_t> m_expanding;  ///< The level step() expands; kept with m_vertex to avoid allocations.
    std::vector<std::byte> m_vertex;
    uint_t m_depth = 0;
    bool m_exhausted = false;

    uint_t& level_slot(uint_t id)
    {
        if (m_level.size() <= id)
            m_level.resize(id + 1, none);
        return m_level[id];
    }
};

}  // namespace ygg::database

#endif
