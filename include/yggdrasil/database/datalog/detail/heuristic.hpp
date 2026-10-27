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
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace ygg::database::datalog::detail
{
class Executor;

/// A* for a weighted relation that only goals read, with a heuristic derived from its rules (Felzenszwalb & McAllester
/// 2007, §5.1): an abstraction keeps of each recursive rule only its literals over static inputs (and what they
/// compute), so that its derivations include the concrete ones at the same weights, and of the key only some columns
/// (a projection, as a pattern database keeps some variables: Culberson & Schaeffer 1998). h(key) is the maximum over
/// the projections of the least abstract weight from the projected key to a projected target, the keys that the goals'
/// other literals give in the current state; a maximum of consistent heuristics is consistent. Each projection's
/// weights are computed for every projected key at once, by Dijkstra's algorithm backward from the targets over its
/// rules reversed (from a head row to the read row, the edge's weight as head weight), once per set of targets, and
/// kept for as long as the static inputs stay the same. A projection whose backward search exceeds a budget of
/// expansions is dropped (implementation decisions: the targets of a policy's states repeat; keeping every column can
/// leave an abstract space far larger than the concrete one that the search visits).
class GoalHeuristic
{
public:
    /// None unless the relation is the stratum's one table, each recursive rule reads it once and adds weights only
    /// (+), and the goals' literals bind some key columns of the targets; a projection keeps only such columns, and is
    /// kept if the static literals of every rule bind
    /// the read row's kept columns and the edge's weight from the head's kept columns.
    static std::unique_ptr<GoalHeuristic> make(CompiledProgram& program, const StratumInfo& stratum, std::span<const size_t> goals);

    /// Reads the targets of the current state, and computes their abstract weights unless known.
    void read_targets(CompiledProgram& program, Executor& executor);
    /// The heuristic of the key; Arithmetic::infinity if no target is reachable from it in a projection.
    [[nodiscard]] uint_t operator()(std::span<const uint_t> key);
    [[nodiscard]] size_t memory_usage() const noexcept;

private:
    struct Projection
    {
        std::vector<size_t> columns;
        /// The table from which the reversed rules read a projected head row.
        size_t scratch;
        /// Reversed abstract rules: the first literal reads a projected head row from `scratch`, the head is the
        /// projected read row with the edge's weight.
        std::vector<CompiledRule> edges;
        RawArraySet<uint_t> keys;
        /// Per set of projected targets (its sorted rows, flattened), the abstract weight to them of each key in
        /// `keys`, infinity for keys beyond its size; `current` is that of the current targets.
        std::map<std::vector<uint_t>, std::vector<uint_t>> weights;
        const std::vector<uint_t>* current = nullptr;
        bool usable = true;
    };
    std::vector<Projection> m_projections;
    /// The key columns that the goals' other literals bind, and per goal those literals with these columns of the
    /// read's key as head.
    std::vector<size_t> m_target_columns;
    std::vector<CompiledRule> m_targets;
    RawArraySet<uint_t> m_target_keys;
    std::vector<Version> m_reads;
    std::vector<std::vector<uint_t>> m_derived;
    std::vector<uint_t> m_key;

    explicit GoalHeuristic(size_t key_arity) : m_target_keys(key_arity) {}
    /// The abstract weights to the projected targets, by Dijkstra's algorithm backward from them; none if the search
    /// exceeds the budget.
    std::optional<std::vector<uint_t>> backward(CompiledProgram& program, Executor& executor, Projection& projection, const RawArraySet<uint_t>& targets);
};
}  // namespace ygg::database::datalog::detail

#endif
