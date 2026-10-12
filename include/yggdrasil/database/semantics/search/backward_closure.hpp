/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_SEARCH_BACKWARD_CLOSURE_HPP_
#define YGG_DATABASE_SEMANTICS_SEARCH_BACKWARD_CLOSURE_HPP_

#include "yggdrasil/database/semantics/search/search_tree.hpp"
#include "yggdrasil/database/semantics/search/concepts.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace ygg::database
{

/// A breadth-first search backwards from the targets, advanced one level at a time over the vertices of a
/// SearchTree, as in perimeter search (Dillenburg & Nelson, "Perimeter Search", Artificial Intelligence 1994;
/// Manzini, "BIDA*: An Improved Perimeter Search Algorithm", Artificial Intelligence 1995). Found vertices get their
/// exact distance as level and a next pointer to their BFS parent. Vertices not yet found are at least one beyond the
/// last level, and once the frontier runs dry no other vertex can reach a target.
///
/// These bounds form a consistent heuristic of their own. Edges removed after a level keep the levels lower bounds;
/// added edges invalidate them, and the caller restarts the search.
class BackwardClosure
{
public:
    static constexpr uint_t none = SearchTree::none;
    static constexpr uint_t infinity = SearchTree::infinity;

    void start(std::span<const uint_t> targets)
    {
        m_level.clear();
        m_frontier.clear();
        for (const auto target : targets)
        {
            level_slot(target) = 0;
            m_frontier.push_back(target);
        }
        m_depth = 0;
        m_complete = false;
    }

    /// Advances one level; returns the number of vertices found.
    template<PredecessorOracle Predecessors, DistanceHeuristic H>
    size_t step(SearchTree& tree, Predecessors& predecessors, const H& heuristic)
    {
        const auto frontier = std::move(m_frontier);
        m_frontier.clear();
        const auto level = m_depth + 1;
        for (const auto parent : frontier)
        {
            const auto target = std::vector<std::byte>(tree.vertex(parent).begin(), tree.vertex(parent).end());
            predecessors.predecessors(target,
                                      [&](std::span<const std::byte> from)
                                      {
                                          const auto id = tree.ensure(from, heuristic.estimate(from));
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
        m_complete = m_frontier.empty();
        return m_frontier.size();
    }

    bool complete() const noexcept { return m_complete; }
    size_t frontier_size() const noexcept { return m_frontier.size(); }
    /// The exact distance of a found vertex, or none.
    uint_t level(uint_t id) const noexcept { return id < m_level.size() ? m_level[id] : none; }
    /// A lower bound on any vertex's distance: its level if found, otherwise one beyond the last level, or infinity
    /// once the closure is complete.
    uint_t bound(std::optional<uint_t> id) const noexcept
    {
        if (id && level(*id) != none)
            return level(*id);
        return m_complete ? infinity : m_depth + 1;
    }

private:
    std::vector<uint_t> m_level;  ///< By the tree's vertex index.
    std::vector<uint_t> m_frontier;
    uint_t m_depth = 0;
    bool m_complete = false;

    uint_t& level_slot(uint_t id)
    {
        if (m_level.size() <= id)
            m_level.resize(id + 1, none);
        return m_level[id];
    }
};

}  // namespace ygg::database

#endif
