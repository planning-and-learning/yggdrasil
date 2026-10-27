/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_DATALOG_DETAIL_GOAL_SEARCH_HPP_
#define YGG_DATABASE_DATALOG_DETAIL_GOAL_SEARCH_HPP_

#include "yggdrasil/database/datalog/detail/program.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace ygg::database::datalog::detail
{
class Executor;

/// The search of a weighted relation R that only goals read (WeightedStratum): the stratum's one table, rules that read
/// it once and add weights only (+), and goals whose other literals give the targets, the keys of R they accept.
struct GoalSearch
{
    size_t table;
    /// The recursive rules, each with its one read of R.
    std::vector<std::pair<size_t, size_t>> rules;
    /// The key columns of R that every goal's other literals bind, and per goal those literals with these columns of
    /// its read's key as head.
    std::vector<size_t> target_columns;
    std::vector<CompiledRule> targets;
    /// Per goal, the key columns of its read of R that are its key.
    std::vector<std::vector<size_t>> goal_columns;

    /// None unless the stratum has this shape.
    static std::optional<GoalSearch> make(CompiledProgram& program, const StratumInfo& stratum, std::span<const size_t> goals);
    /// The recursive rules reversed, over the key columns `columns`: the first literal reads a head row (its `columns`,
    /// then a weight) from `scratch`, the head is the read row's `columns` with the edge's weight (the read row's weight
    /// taken as 0). With `abstract`, only literals over static inputs (and what they compute) are kept, so that the
    /// derivations include the concrete ones in every state; otherwise every literal. Literals left unbound are dropped,
    /// which only adds derivations. None if a rule cannot bind the read row's `columns` and the weight.
    [[nodiscard]] std::optional<std::vector<CompiledRule>>
    reverse(CompiledProgram& program, std::span<const size_t> columns, size_t scratch, bool abstract) const;
    /// Adds the targets of the current state, rows of `target_columns`.
    void read_targets(Executor& executor, std::vector<Version>& reads, RawArraySet<uint_t>& keys) const;
};

/// A* for a GoalSearch, with a heuristic derived from its rules (Felzenszwalb & McAllester 2007, §5.1): an abstraction
/// keeps of each recursive rule only its literals over static inputs (and what they compute), so that its derivations
/// include the concrete ones at the same weights, and of the key only some columns (a projection, as a pattern database
/// keeps some variables: Culberson & Schaeffer 1998). h(key) is the maximum over the projections of the least abstract
/// weight from the projected key to a projected target; a maximum of consistent heuristics is consistent. Each
/// projection's weights are computed for every projected key at once, by Dijkstra's algorithm backward from the
/// targets over its reversed rules, once per set of targets, and kept for as long as the static inputs stay the same.
/// A projection whose backward search exceeds a budget of expansions is dropped (implementation decisions: the targets
/// of a policy's states repeat; keeping every column can leave an abstract space far larger than the concrete one that
/// the search visits).
class GoalHeuristic
{
public:
    /// None unless some projection onto target columns has reversed abstract rules.
    static std::unique_ptr<GoalHeuristic> make(CompiledProgram& program, const GoalSearch& search);

    /// Reads the targets of the current state, and computes their abstract weights unless known.
    void read_targets(CompiledProgram& program, Executor& executor);
    /// The heuristic of the key; Arithmetic::infinity if no target is reachable from it in a projection.
    [[nodiscard]] uint_t operator()(std::span<const uint_t> key);
    [[nodiscard]] size_t memory_usage() const noexcept;

private:
    struct Projection
    {
        std::vector<size_t> columns;
        size_t scratch;
        std::vector<CompiledRule> edges;
        RawArraySet<uint_t> keys;
        /// Per set of projected targets (its sorted rows, flattened), the abstract weight to them of each key in
        /// `keys`, infinity for keys beyond its size; `current` is that of the current targets.
        std::map<std::vector<uint_t>, std::vector<uint_t>> weights;
        const std::vector<uint_t>* current = nullptr;
        bool usable = true;
    };
    GoalSearch m_search;
    std::vector<Projection> m_projections;
    RawArraySet<uint_t> m_target_keys;
    std::vector<Version> m_reads;
    std::vector<std::vector<uint_t>> m_derived;
    std::vector<uint_t> m_key;

    explicit GoalHeuristic(const GoalSearch& search) : m_search(search), m_target_keys(search.target_columns.size()) {}
    /// The abstract weights to the projected targets, by Dijkstra's algorithm backward from them; none if the search
    /// exceeds the budget.
    std::optional<std::vector<uint_t>> backward(CompiledProgram& program, Executor& executor, Projection& projection, const RawArraySet<uint_t>& targets);
};

/// The other side of a bidirectional search for a GoalSearch with one goal whose targets bind every key column: per key
/// of the goal, a breadth-first search backward from the key's targets over the rules reversed with all their
/// literals, in the current state, so that its rows are keys of R from which a target is derivable. It advances by one
/// row per row the stratum settles for the goal key (bidirectional search, Pohl 1971; whichever side is smaller is
/// exhausted first, as in search/must_pass.hpp). A source among its rows ends it: the stratum's search reaches a target.
/// Exhausted without one, it proves that the goal key's goal has no row (implementation decisions).
class GoalFrontier
{
public:
    /// None unless the stratum has one goal, its targets bind every key column and every rule can be reversed.
    static std::unique_ptr<GoalFrontier> make(CompiledProgram& program, const GoalSearch& search);

    /// Starts anew with the targets of the current state.
    void read_targets(CompiledProgram& program, Executor& executor);
    /// A source: a key of R that the stratum's base rules derive.
    void add_source(std::span<const uint_t> key);
    /// Advances the search of the key's goal key by one row.
    void advance(CompiledProgram& program, Executor& executor, std::span<const uint_t> key);
    /// Whether the key's goal key is known to have no target that the stratum reaches.
    [[nodiscard]] bool excluded(std::span<const uint_t> key);
    [[nodiscard]] size_t memory_usage() const noexcept;

private:
    enum class Status : uint8_t
    {
        SEARCHING,
        REACHABLE,
        UNREACHABLE,
    };
    /// The backward search of one goal key: its rows (indices into m_rows) in the order found.
    struct Side
    {
        std::vector<uint_t> rows;
        size_t next = 0;
        Status status = Status::SEARCHING;
    };
    GoalSearch m_search;
    std::vector<size_t> m_goal_columns;
    size_t m_scratch;
    std::vector<CompiledRule> m_edges;
    /// The goal keys with targets, each with its side; the rows found backward; the sources.
    RawArraySet<uint_t> m_goal_keys;
    std::vector<Side> m_sides;
    RawArraySet<uint_t> m_rows;
    RawArraySet<uint_t> m_sources;
    std::vector<Version> m_reads;
    std::vector<std::vector<uint_t>> m_derived;
    std::vector<uint_t> m_key;

    GoalFrontier(const GoalSearch& search, std::vector<size_t> goal_columns, size_t scratch, std::vector<CompiledRule> edges);
    /// The goal key of a key of R; a goal without key columns has one goal key, (0).
    std::span<const uint_t> goal_key(std::span<const uint_t> key);
};
}  // namespace ygg::database::datalog::detail

#endif
