/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_SEARCH_SEARCH_TREE_HPP_
#define YGG_DATABASE_SEMANTICS_SEARCH_SEARCH_TREE_HPP_

#include <urd/repr/sequences/sequence.hpp>
#include <urd/repr/pools/pools.hpp>
#include <urd/store.hpp>
#include "yggdrasil/database/semantics/search/concepts.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace ygg::database
{

/// Vertices of a search as byte rows of one size, interned in a urd pool (rows stored flat, never moved by inserts): a
/// vertex's ID is its handle's value.
using VertexSet = urd::Store<urd::pool<urd::Store<urd::sequence<std::byte>, urd::frozen>>>;

/// The ID of a stored vertex, or none.
inline std::optional<uint_t> vertex_id(const VertexSet& vertices, std::span<const std::byte> vertex)
{
    const auto found = vertices.find(vertex);
    return found.is_none() ? std::nullopt : std::optional<uint_t>(found.value);
}
/// The ID of a vertex, stored where it is new.
inline uint_t insert_vertex(VertexSet& vertices, std::span<const std::byte> vertex) { return vertices.insert(vertex).first.value; }
/// The vertex of an ID.
inline std::span<const std::byte> vertex_at(const VertexSet& vertices, uint_t id) { return vertices.view(VertexSet::handle_type(id)); }

/// The stored vertices of searches towards fixed targets: per vertex a lower bound on its distance (learned) and one
/// next pointer along a found path. A vertex whose next chain reaches a target, with values that drop by one per
/// existing edge, has an exact value: the goal condition of Multipath Generalized Adaptive A* (Hernández, Asín &
/// Baier, "Reusing Previously Found A* Paths for Fast Goal-Directed Navigation in Dynamic Terrain", AAAI 2015).
/// Edges of a chain are checked lazily, and a check is kept until the graph's version changes.
///
/// The tree knows edges only through EdgeCheck; the caller supplies the value used along a chain (e.g. the larger of
/// the learned distance and an exact backward level). incremental::MinDistance keeps it across searches and repairs it
/// (incremental/search/tree_repair.hpp).
class SearchTree
{
public:
    SearchTree() : m_vertices() {}

    void clear()
    {
        m_vertices.clear();
        m_nodes.clear();
    }

    [[nodiscard]] size_t size() const noexcept { return m_nodes.size(); }
    [[nodiscard]] std::optional<uint_t> find(std::span<const std::byte> vertex) const { return vertex_id(m_vertices, vertex); }
    [[nodiscard]] std::span<const std::byte> vertex(uint_t id) const { return vertex_at(m_vertices, id); }

    /// The stored index of a vertex, inserting it with its heuristic estimate as learned distance. The estimate is
    /// computed only on insertion.
    template<DistanceHeuristic H>
    uint_t ensure(std::span<const std::byte> vertex, const H& heuristic)
    {
        if (const auto stored = vertex_id(m_vertices, vertex))
            return *stored;
        const auto estimate = heuristic.estimate(vertex);
        const auto id = insert_vertex(m_vertices, vertex);
        m_nodes.push_back(Node { estimate });
        return id;
    }
    /// The learned distance of a vertex: its stored value, or the heuristic's estimate if it is not stored.
    template<DistanceHeuristic H>
    [[nodiscard]] uint_t value(std::span<const std::byte> vertex, const H& heuristic) const
    {
        const auto stored = vertex_id(m_vertices, vertex);
        return stored ? learned(*stored) : heuristic.estimate(vertex);
    }

    [[nodiscard]] uint_t learned(uint_t id) const noexcept { return m_nodes[id].learned; }
    /// Raises the learned distance to a bound a search established (Generalized Adaptive A*).
    void learn(uint_t id, uint_t bound) noexcept { m_nodes[id].learned = std::max(m_nodes[id].learned, bound); }
    /// Lowers the learned distance and drops the next pointer, after edges were added.
    void lower(uint_t id, uint_t bound) noexcept
    {
        m_nodes[id].learned = bound;
        set_next(id, none);
    }
    [[nodiscard]] uint_t next(uint_t id) const noexcept { return m_nodes[id].next; }
    void link(uint_t from, uint_t to) noexcept { set_next(from, to); }

    /// Starts a new search: chain checks of earlier searches are forgotten.
    void begin_search() noexcept { ++m_epoch; }

    /// Whether the next chain from `start` reaches a target with values that drop by one per existing edge. Edge
    /// checks hold until `version` changes.
    template<typename Value, typename IsTarget, EdgeCheck Edges>
        requires std::invocable<const Value&, uint_t> && std::predicate<const IsTarget&, uint_t>
    bool on_valid_path(uint_t start, const Value& value, const IsTarget& is_target, Edges& edges, uint64_t version)
    {
        auto& chain = m_chain;
        chain.clear();
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
    static constexpr uint64_t unchecked = std::numeric_limits<uint64_t>::max();

    struct Node
    {
        uint_t learned = 0;
        uint_t next = none;
        uint64_t checked = 0;               ///< Epoch of the last chain check.
        bool valid = false;                 ///< Its result.
        uint64_t edge_version = unchecked;  ///< Graph version at which the edge to next was checked.
    };

    /// A new next pointer has an unchecked edge.
    void set_next(uint_t id, uint_t next) noexcept
    {
        m_nodes[id].next = next;
        m_nodes[id].edge_version = unchecked;
    }

    VertexSet m_vertices;
    urd::Store<urd::sequence<Node>> m_nodes;
    uint64_t m_epoch = 0;
    urd::Store<urd::sequence<uint_t>> m_chain;  ///< Scratch of on_valid_path(), kept to avoid allocations.
};

}  // namespace ygg::database

#endif
