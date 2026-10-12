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

/// A SearchTree kept across searches towards fixed targets while the graph changes.
///
/// Learning follows Generalized Adaptive A* (Sun, Koenig & Yeoh, "Generalized Adaptive A*", AAMAS 2008): bounds
/// learned by closed vertices stay admissible and consistent with the caller's heuristic, so removed edges keep them
/// valid. After edges were added, repair() is GAA*'s ReestablishConsistency, a Dijkstra-like lowering over stored
/// predecessors; unstored vertices keep the heuristic's estimate. Next pointers are relinked through support pointers
/// as in Multipath Generalized Adaptive A* (Hernández, Asín & Baier, AAAI 2015).
class AdaptivePathTree
{
public:
    static constexpr uint_t none = SearchTree::none;
    static constexpr uint_t infinity = SearchTree::infinity;

    explicit AdaptivePathTree(size_t vertex_size) : m_tree(vertex_size) {}

    SearchTree& tree() noexcept { return m_tree; }
    const SearchTree& tree() const noexcept { return m_tree; }

    void clear()
    {
        m_tree.clear();
        m_support.clear();
    }

    /// GAA*'s ReestablishConsistency with MPGAA*'s support pointers, over stored vertices, after `added` edges;
    /// returns the number of lowered vertices.
    template<PredecessorOracle Predecessors, DistanceHeuristic H>
    size_t repair(const EdgeList& added, Predecessors& predecessors, const H& heuristic)
    {
        using Entry = std::pair<uint_t, uint_t>;
        auto queue = std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>>();
        const auto lower = [&](uint_t from, uint_t to)
        {
            if (m_tree.learned(to) == infinity || m_tree.learned(from) <= m_tree.learned(to) + 1)
                return;
            m_tree.lower(from, m_tree.learned(to) + 1);
            support(from) = to;
            queue.emplace(m_tree.learned(from), from);
        };
        for (size_t i = 0; i < added.size(); ++i)
            if (const auto from = m_tree.find(added.from(i)))
                lower(*from, m_tree.ensure(added.to(i), heuristic.estimate(added.to(i))));
        auto lowered = size_t(0);
        while (!queue.empty())
        {
            const auto [learned, id] = queue.top();
            queue.pop();
            if (learned != m_tree.learned(id))
                continue;
            ++lowered;
            const auto supporter = support(id);
            if (supporter != none && (m_tree.learned(supporter) == 0 || m_tree.next(supporter) != none))
                m_tree.link(id, supporter);
            const auto target = std::vector<std::byte>(m_tree.vertex(id).begin(), m_tree.vertex(id).end());
            predecessors.predecessors(target,
                                      [&](std::span<const std::byte> from)
                                      {
                                          if (const auto stored = m_tree.find(from))
                                              lower(*stored, id);
                                          return true;
                                      });
        }
        return lowered;
    }

private:
    SearchTree m_tree;
    std::vector<uint_t> m_support;  ///< By the tree's vertex index; grows with it.

    uint_t& support(uint_t id)
    {
        if (m_support.size() <= id)
            m_support.resize(m_tree.size(), none);
        return m_support[id];
    }
};

}  // namespace ygg::database::incremental

#endif
