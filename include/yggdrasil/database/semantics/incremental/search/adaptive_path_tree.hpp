/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_INCREMENTAL_SEARCH_ADAPTIVE_PATH_TREE_HPP_
#define YGG_DATABASE_SEMANTICS_INCREMENTAL_SEARCH_ADAPTIVE_PATH_TREE_HPP_

#include "yggdrasil/containers/raw_array_set.hpp"
#include "yggdrasil/database/semantics/search/concepts.hpp"
#include "yggdrasil/database/semantics/search/search_tree.hpp"

#include <cstddef>
#include <functional>
#include <queue>
#include <span>
#include <utility>
#include <vector>

namespace ygg::database::incremental
{

/// Edges between vertices given by their bytes; each vertex is stored once.
class EdgeList
{
public:
    explicit EdgeList(size_t vertex_size) : m_vertices(vertex_size) {}

    void add(std::span<const std::byte> from, std::span<const std::byte> to) { m_edges.emplace_back(m_vertices.insert(from), m_vertices.insert(to)); }
    void clear() noexcept
    {
        m_vertices.clear();
        m_edges.clear();
    }
    bool empty() const noexcept { return m_edges.empty(); }
    size_t size() const noexcept { return m_edges.size(); }
    std::span<const std::byte> from(size_t i) const { return m_vertices[m_edges[i].first]; }
    std::span<const std::byte> to(size_t i) const { return m_vertices[m_edges[i].second]; }

private:
    RawArraySet<std::byte, 64> m_vertices;
    std::vector<std::pair<uint_t, uint_t>> m_edges;
};

/// Generalized Adaptive A*'s ReestablishConsistency (Sun, Koenig & Yeoh, "Generalized Adaptive A*", AAMAS 2008) on a
/// SearchTree kept across searches towards fixed targets, after `added` edges: a Dijkstra-like lowering of learned
/// distances over stored predecessors; unstored vertices keep the heuristic's estimate. Learned bounds stay admissible
/// and consistent with the heuristic, so removed edges keep them valid. A lowered vertex's next pointer is relinked to
/// the vertex that lowered it, as with the support pointers of Multipath Generalized Adaptive A* (Hernández, Asín &
/// Baier, AAAI 2015); SearchTree::on_valid_path() checks the link when it is used. Returns the number of lowered
/// vertices.
template<PredecessorOracle Predecessors, DistanceHeuristic H>
size_t repair(SearchTree& tree, const EdgeList& added, Predecessors& predecessors, const H& heuristic)
{
    using Entry = std::pair<uint_t, uint_t>;
    auto queue = std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>>();
    const auto lower = [&](uint_t from, uint_t to)
    {
        if (tree.learned(to) == infinity || tree.learned(from) <= tree.learned(to) + 1)
            return;
        tree.lower(from, tree.learned(to) + 1);
        tree.link(from, to);
        queue.emplace(tree.learned(from), from);
    };
    for (size_t i = 0; i < added.size(); ++i)
        if (const auto from = tree.find(added.from(i)))
            lower(*from, tree.ensure(added.to(i), heuristic.estimate(added.to(i))));
    auto lowered = size_t(0);
    while (!queue.empty())
    {
        const auto [learned, id] = queue.top();
        queue.pop();
        if (learned != tree.learned(id))
            continue;
        ++lowered;
        const auto target = std::vector<std::byte>(tree.vertex(id).begin(), tree.vertex(id).end());
        predecessors.predecessors(target,
                                  [&](std::span<const std::byte> from)
                                  {
                                      if (const auto stored = tree.find(from))
                                          lower(*stored, id);
                                      return true;
                                  });
    }
    return lowered;
}

}  // namespace ygg::database::incremental

#endif
