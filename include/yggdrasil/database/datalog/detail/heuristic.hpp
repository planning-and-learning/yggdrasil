/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_DATALOG_DETAIL_HEURISTIC_HPP_
#define YGG_DATABASE_DATALOG_DETAIL_HEURISTIC_HPP_

#include "yggdrasil/database/datalog/detail/program.hpp"

#include <cstddef>
#include <map>
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
/// weight from the key to a target, the keys that the goals' other literals give in the current state. It is computed
/// for every key at once, by Dijkstra's algorithm backward from the targets over the abstract rules reversed (from a
/// head row to the read row, the edge's weight as head weight), once per set of targets, and kept for as long as the
/// static inputs stay the same (implementation decision: the targets of a policy's states repeat).
class GoalHeuristic
{
public:
    /// None unless the relation is the stratum's one table, each recursive rule reads it once and adds weights only
    /// (+), the static literals bind the read row's key from the head's key, and the goals' literals bind the targets.
    static std::unique_ptr<GoalHeuristic> make(CompiledProgram& program, const StratumInfo& stratum, std::span<const size_t> goals);

    /// Reads the targets of the current state, and computes their abstract weights unless known.
    void read_targets(CompiledProgram& program, Executor& executor);
    /// The least abstract weight from the key to a target; Arithmetic::infinity if no target is reachable.
    [[nodiscard]] uint_t operator()(std::span<const uint_t> key) const;
    [[nodiscard]] size_t memory_usage() const noexcept;

private:
    /// A reversed abstract rule: its first literal reads a head row from `m_scratch`, its head is the read row's key
    /// with the edge's weight.
    std::vector<CompiledRule> m_edges;
    size_t m_scratch;
    /// Per goal, its literals other than its read of the relation, with the key of that read as head.
    std::vector<CompiledRule> m_targets;
    RawArraySet<uint_t> m_target_keys;
    RawArraySet<uint_t> m_keys;
    /// Per set of targets (its sorted rows, flattened), the abstract weight to them of each key in m_keys, infinity for
    /// keys beyond its size; `m_current` is that of the current targets.
    std::map<std::vector<uint_t>, std::vector<uint_t>> m_weights;
    const std::vector<uint_t>* m_current = nullptr;
    std::vector<Version> m_reads;
    std::vector<std::vector<uint_t>> m_derived;

    GoalHeuristic(size_t scratch, size_t key_arity) : m_scratch(scratch), m_target_keys(key_arity), m_keys(key_arity) {}
    /// The abstract weights to the current targets, by Dijkstra's algorithm backward from them.
    std::vector<uint_t> backward(CompiledProgram& program, Executor& executor);
};
}  // namespace ygg::database::datalog::detail

#endif
