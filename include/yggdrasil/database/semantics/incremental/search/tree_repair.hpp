/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_INCREMENTAL_SEARCH_TREE_REPAIR_HPP_
#define YGG_DATABASE_SEMANTICS_INCREMENTAL_SEARCH_TREE_REPAIR_HPP_

#include "yggdrasil/containers/raw_array_set.hpp"
#include "yggdrasil/database/semantics/search/concepts.hpp"
#include "yggdrasil/database/semantics/search/search_tree.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
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
    VertexSet m_vertices;
    std::vector<std::pair<uint_t, uint_t>> m_edges;
};

/// Generalized Adaptive A*'s ReestablishConsistency (Sun, Koenig & Yeoh, "Generalized Adaptive A*", AAMAS 2008) on a
/// SearchTree kept across searches towards fixed targets, after `added` edges: a Dijkstra-like lowering of learned
/// distances over stored predecessors; unstored vertices keep the heuristic's estimate. Learned bounds stay admissible
/// and consistent with the heuristic, so removed edges keep them valid. A lowered vertex's next pointer is relinked to
/// the vertex that lowered it, as with the support pointers of Multipath Generalized Adaptive A* (Hernández, Asín &
/// Baier, AAAI 2015); SearchTree::on_valid_path() checks the link when it is used. The target of an added edge is
/// stored only if it lowers the edge's source. Owns its queue, so repeated repairs do not allocate.
class TreeRepair
{
public:
    /// Returns the number of lowered vertices.
    template<PredecessorOracle Predecessors, DistanceHeuristic H>
    size_t run(SearchTree& tree, const EdgeList& added, Predecessors& predecessors, const H& heuristic)
    {
        m_queue.clear();
        for (size_t i = 0; i < added.size(); ++i)
            if (const auto from = tree.find(added.from(i)); from && lowers(tree, *from, tree.value(added.to(i), heuristic)))
                lower(tree, *from, tree.ensure(added.to(i), heuristic));
        auto lowered = size_t(0);
        while (!m_queue.empty())
        {
            std::ranges::pop_heap(m_queue, std::greater<> {});
            const auto [learned, id] = m_queue.back();
            m_queue.pop_back();
            if (learned != tree.learned(id))
                continue;
            ++lowered;
            m_vertex.assign(tree.vertex(id).begin(), tree.vertex(id).end());
            predecessors.predecessors(m_vertex,
                                      [&](std::span<const std::byte> from)
                                      {
                                          if (const auto stored = tree.find(from); stored && lowers(tree, *stored, tree.learned(id)))
                                              lower(tree, *stored, id);
                                          return true;
                                      });
        }
        return lowered;
    }

private:
    std::vector<std::pair<uint_t, uint_t>> m_queue;  ///< A min-heap of (learned, vertex).
    std::vector<std::byte> m_vertex;                 ///< A copy of the lowered vertex: storing vertices may move them.

    static bool lowers(const SearchTree& tree, uint_t from, uint_t to_value) noexcept { return to_value != infinity && tree.learned(from) > to_value + 1; }
    void lower(SearchTree& tree, uint_t from, uint_t to)
    {
        tree.lower(from, tree.learned(to) + 1);
        tree.link(from, to);
        m_queue.emplace_back(tree.learned(from), from);
        std::ranges::push_heap(m_queue, std::greater<> {});
    }
};

}  // namespace ygg::database::incremental

#endif
