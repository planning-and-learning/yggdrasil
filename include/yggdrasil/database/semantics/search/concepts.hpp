/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_SEARCH_CONCEPTS_HPP_
#define YGG_DATABASE_SEMANTICS_SEARCH_CONCEPTS_HPP_

#include "yggdrasil/core/config.hpp"

#include <concepts>
#include <cstddef>
#include <span>

namespace ygg::database
{

/// An estimate of a vertex's distance to the targets; uint_t's maximum means no target is reachable.
///
/// Contract, relied on for exactness: the estimate is consistent (h(u) <= 1 + h(v) for every edge (u, v), and 0 on
/// targets) in every graph the inputs can form while the targets stay unchanged, i.e. it depends on the targets and on
/// static inputs only. Learned distances are repaired against it after edge additions, which is only sound if it does
/// not drop with them; Generalized Adaptive A* assumes the same of its user heuristic (Sun, Koenig & Yeoh 2008).
template<typename H>
concept DistanceHeuristic = requires(const H& h, std::span<const std::byte> vertex) {
    { h.estimate(vertex) } -> std::same_as<uint_t>;
};

/// The zero estimate: breadth-first order.
struct BlindDistanceHeuristic
{
    uint_t estimate(std::span<const std::byte>) const noexcept { return 0; }
};

/// Calls on_vertex(w) for each edge (v, w); stops when it returns false.
template<typename O>
concept SuccessorOracle = requires(O& oracle, std::span<const std::byte> vertex) {
    oracle.successors(vertex, [](std::span<const std::byte>) { return true; });
};

/// Calls on_vertex(u) for each edge (u, v); stops when it returns false.
template<typename O>
concept PredecessorOracle = requires(O& oracle, std::span<const std::byte> vertex) {
    oracle.predecessors(vertex, [](std::span<const std::byte>) { return true; });
};

/// Whether the edge (u, v) exists.
template<typename O>
concept EdgeCheck = requires(O& oracle, std::span<const std::byte> from, std::span<const std::byte> to) {
    { oracle.edge_exists(from, to) } -> std::same_as<bool>;
};

}  // namespace ygg::database

#endif
