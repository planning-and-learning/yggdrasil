/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_SEARCH_SEARCH_TREE_HPP_
#define YGG_DATABASE_SEMANTICS_SEARCH_SEARCH_TREE_HPP_

#include "yggdrasil/containers/raw_array_set.hpp"
#include "yggdrasil/database/semantics/search/concepts.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace ygg::database
{

/// The stored vertices of searches towards fixed targets: per vertex a lower bound on its distance (learned) and one
/// next pointer along a found path. A vertex whose next chain reaches a target, with values that drop by one per
/// existing edge, has an exact value: the goal condition of Multipath Generalized Adaptive A* (Hernández, Asín &
/// Baier, "Reusing Previously Found A* Paths for Fast Goal-Directed Navigation in Dynamic Terrain", AAAI 2015).
/// Edges of a chain are checked lazily, and a check is kept until the graph's version changes.
///
/// The tree knows edges only through EdgeCheck; the caller supplies the value used along a chain (e.g. the larger of
/// the learned distance and an exact backward level). incremental::MinDistance keeps it across searches and repairs it
/// (incremental/search/adaptive_path_tree.hpp).
class SearchTree
{
public:
    explicit SearchTree(size_t vertex_size) : m_vertices(vertex_size) {}

    void clear()
    {
        m_vertices.clear();
        m_nodes.clear();
    }

    size_t size() const noexcept { return m_nodes.size(); }
    std::optional<uint_t> find(std::span<const std::byte> vertex) const { return m_vertices.find(vertex); }
    std::span<const std::byte> vertex(uint_t id) const { return m_vertices[id]; }

    /// The stored index of a vertex, inserting it with its heuristic estimate as learned distance.
    uint_t ensure(std::span<const std::byte> vertex, uint_t estimate)
    {
        if (const auto stored = m_vertices.find(vertex))
            return *stored;
        const auto id = m_vertices.insert(vertex);
        m_nodes.push_back(Node { estimate });
        return id;
    }

    uint_t learned(uint_t id) const noexcept { return m_nodes[id].learned; }
    /// Raises the learned distance to a bound a search established (Generalized Adaptive A*).
    void learn(uint_t id, uint_t bound) noexcept { m_nodes[id].learned = std::max(m_nodes[id].learned, bound); }
    /// Lowers the learned distance and drops the next pointer, after edges were added.
    void lower(uint_t id, uint_t bound) noexcept
    {
        m_nodes[id].learned = bound;
        set_next(id, none);
    }
    uint_t next(uint_t id) const noexcept { return m_nodes[id].next; }
    void link(uint_t from, uint_t to) noexcept { set_next(from, to); }

    /// Starts a new search: chain checks of earlier searches are forgotten.
    void begin_search() noexcept { ++m_epoch; }

    /// Whether the next chain from `start` reaches a target with values that drop by one per existing edge. Edge
    /// checks hold until `version` changes.
    template<typename Value, typename IsTarget, EdgeCheck Edges>
        requires std::invocable<Value&, uint_t> && std::predicate<IsTarget&, uint_t>
    bool on_valid_path(uint_t start, Value&& value, IsTarget&& is_target, Edges& edges, uint32_t version)
    {
        auto chain = std::vector<uint_t>();
        auto result = false;
        for (auto u = start;;)
        {
            if (m_nodes[u].checked == m_epoch)
            {
                result = m_nodes[u].valid;
                break;
            }
            chain.push_back(u);
            if (value(u) == 0 && is_target(u))
            {
                result = true;
                break;
            }
            const auto v = m_nodes[u].next;
            if (v == none || value(v) == infinity || value(u) != value(v) + 1)
                break;
            if (m_nodes[u].edge_version != version)
            {
                if (!edges.edge_exists(vertex(u), vertex(v)))
                {
                    set_next(u, none);
                    break;
                }
                m_nodes[u].edge_version = version;
            }
            u = v;
        }
        for (const auto u : chain)
        {
            m_nodes[u].checked = m_epoch;
            m_nodes[u].valid = result;
        }
        return result;
    }

private:
    static constexpr uint32_t unchecked = std::numeric_limits<uint32_t>::max();

    struct Node
    {
        uint_t learned = 0;
        uint_t next = none;
        uint32_t checked = 0;              ///< Epoch of the last chain check.
        bool valid = false;                ///< Its result.
        uint32_t edge_version = unchecked; ///< Graph version at which the edge to next was checked.
    };

    /// A new next pointer has an unchecked edge.
    void set_next(uint_t id, uint_t next) noexcept
    {
        m_nodes[id].next = next;
        m_nodes[id].edge_version = unchecked;
    }

    RawArraySet<std::byte, 64> m_vertices;
    std::vector<Node> m_nodes;
    uint32_t m_epoch = 0;
};

}  // namespace ygg::database

#endif
