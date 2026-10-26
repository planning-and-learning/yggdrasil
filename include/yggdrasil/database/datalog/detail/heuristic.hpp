/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_DATALOG_DETAIL_HEURISTIC_HPP_
#define YGG_DATABASE_DATALOG_DETAIL_HEURISTIC_HPP_

#include "yggdrasil/database/datalog/detail/program.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace ygg::database::datalog::detail
{
class Executor;

/// A* for a weighted relation that only goals read, with a heuristic derived from its rules (Felzenszwalb & McAllester
/// 2007, §5.1): the abstraction keeps of each recursive rule only its literals over static inputs (and what they
/// compute), so that its derivations include the concrete ones at the same weights. h(key) is the least abstract
/// weight from the key to a target, the keys that the goals' other literals give in the current state. The abstract
/// weights from a key to every key are computed once, by Dijkstra's algorithm over the abstract rules, and kept for
/// as long as the static inputs stay the same; a change of the targets only changes which of them h reads.
class GoalHeuristic
{
public:
    /// None unless the relation is the stratum's one table, each recursive rule reads it once and adds weights only
    /// (+), and the static literals bind the head's key from the read row's key, and the goals' literals bind the
    /// targets.
    static std::unique_ptr<GoalHeuristic> make(CompiledProgram& program, const StratumInfo& stratum, std::span<const size_t> goals);

    /// Reads the targets of the current state.
    void read_targets(CompiledProgram& program, Executor& executor);
    /// The least abstract weight from the key to a target; Arithmetic::infinity if no target is reachable.
    [[nodiscard]] uint_t operator()(CompiledProgram& program, Executor& executor, std::span<const uint_t> key);
    [[nodiscard]] size_t memory_usage() const noexcept;

private:
    /// An abstract rule: a recursive rule with only its static literals; its read of the relation reads `m_scratch`.
    struct Edge
    {
        CompiledRule rule;
        size_t source {};
    };
    size_t m_scratch;
    std::vector<Edge> m_edges;
    /// Per goal, its literals other than its read of the relation, with the key of that read as head.
    std::vector<CompiledRule> m_targets;
    RawArraySet<uint_t> m_target_keys;
    /// Per key with abstract weights, the keys reachable from it with their weights, in increasing weight.
    RawArraySet<uint_t> m_keys;
    std::vector<std::vector<std::pair<uint_t, uint_t>>> m_rows;
    std::vector<Version> m_reads;
    std::vector<std::vector<uint_t>> m_derived;

    GoalHeuristic(size_t scratch, size_t key_arity) : m_scratch(scratch), m_target_keys(key_arity), m_keys(key_arity) {}
    /// The abstract weights from the key at `index` to every key, by Dijkstra's algorithm.
    void compute_row(CompiledProgram& program, Executor& executor, uint_t index);
};
}  // namespace ygg::database::datalog::detail

#endif
