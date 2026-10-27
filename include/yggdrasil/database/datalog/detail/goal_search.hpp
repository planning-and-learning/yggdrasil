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

/// A rule run from values of some of its terms (Executor::run_bound).
struct BoundRule
{
    CompiledRule rule;
    std::vector<Term> inputs;
    Plan plan;
};

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
    /// The tables that the targets' literals read: the targets change only with them.
    std::vector<size_t> target_tables;

    /// None unless the stratum has this shape.
    static std::optional<GoalSearch> make(CompiledProgram& program, const StratumInfo& stratum, std::span<const size_t> goals);
    /// The recursive rules reversed, over the key columns `columns`: run from a head row's `columns`, they derive the
    /// read row's `columns` with the edge's weight (the read row's weight taken as 0). With `abstract`, only literals over
    /// static inputs (and what they compute) are kept, so that the derivations include the concrete ones in every state,
    /// and rules that keep every column of `columns` (self-loops there) are left out; otherwise every literal. Literals
    /// left unbound are dropped, which only adds derivations. None if a rule cannot bind the read row's `columns` and the
    /// weight.
    [[nodiscard]] std::optional<std::vector<BoundRule>> reverse(CompiledProgram& program, std::span<const size_t> columns, bool abstract) const;
    /// Adds the targets of the current state, rows of `target_columns`.
    void read_targets(Executor& executor, std::vector<Version>& reads, RawArraySet<uint_t>& keys) const;
};

/// A* for a GoalSearch, with a heuristic derived from its rules (Felzenszwalb & McAllester 2007, §5.1): an abstraction
/// keeps of each recursive rule only its literals over static inputs (and what they compute), so that its derivations
/// include the concrete ones at the same weights, and of the key only one column (a single-column pattern database, as
/// n_distance's heuristic: Culberson & Schaeffer 1998). h(key) is the maximum over the target columns of the least
/// abstract weight from the key's column to a target's; a maximum of consistent heuristics is consistent (Holte et al.,
/// AIJ 2006). Each column's weights are computed for every value at once, by Dijkstra's algorithm backward from the
/// targets over its reversed rules, once per set of targets, and kept for as long as the static inputs stay the same
/// (implementation decision: the targets of a policy's states repeat).
class GoalHeuristic
{
public:
    /// None unless some target column has reversed abstract rules.
    static std::unique_ptr<GoalHeuristic> make(CompiledProgram& program, const GoalSearch& search);

    /// Reads the targets of the current state unless `changed` is false and they were read, and computes their
    /// abstract weights unless known.
    void read_targets(Executor& executor, bool changed);
    /// The heuristic of the key; Arithmetic::infinity if no target is reachable from it in a projection.
    [[nodiscard]] uint_t operator()(std::span<const uint_t> key);
    /// Changes whenever read_targets() changes the function, so that bounds learned with it can be discarded.
    [[nodiscard]] uint64_t generation() const noexcept { return m_generation; }
    [[nodiscard]] size_t memory_usage() const noexcept;

private:
    struct Projection
    {
        std::vector<size_t> columns;
        std::vector<BoundRule> edges;
        RawArraySet<uint_t> keys;
        /// Per set of projected targets (its sorted rows, flattened), the abstract weight to them of each key in
        /// `keys`, infinity for keys beyond its size; `current` is that of the current targets.
        std::map<std::vector<uint_t>, std::vector<uint_t>> weights;
        const std::vector<uint_t>* current = nullptr;
    };
    GoalSearch m_search;
    std::vector<Projection> m_projections;
    uint64_t m_generation = 0;
    bool m_read = false;
    RawArraySet<uint_t> m_target_keys;
    std::vector<Version> m_reads;
    std::vector<std::vector<uint_t>> m_derived;
    std::vector<uint_t> m_key;

    explicit GoalHeuristic(const GoalSearch& search) : m_search(search), m_target_keys(search.target_columns.size()) {}
    /// The abstract weights to the projected targets, by Dijkstra's algorithm backward from them.
    std::vector<uint_t> backward(Executor& executor, Projection& projection, const RawArraySet<uint_t>& targets);
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
    void read_targets(Executor& executor);
    /// A source: a key of R that the stratum's base rules derive.
    void add_source(std::span<const uint_t> key);
    /// Advances the search of the key's goal key by one row.
    void advance(Executor& executor, std::span<const uint_t> key);
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
    std::vector<BoundRule> m_edges;
    /// The goal keys with targets, each with its side; the rows found backward; the sources.
    RawArraySet<uint_t> m_goal_keys;
    std::vector<Side> m_sides;
    RawArraySet<uint_t> m_rows;
    RawArraySet<uint_t> m_sources;
    std::vector<Version> m_reads;
    std::vector<std::vector<uint_t>> m_derived;
    std::vector<uint_t> m_key;

    GoalFrontier(const GoalSearch& search, std::vector<size_t> goal_columns, std::vector<BoundRule> edges);
    /// The goal key of a key of R; a goal without key columns has one goal key, (0).
    std::span<const uint_t> goal_key(std::span<const uint_t> key);
};
/// The search of a unit-cost GoalSearch (base rules of weight 0, recursive rules adding 1) whose targets bind every key
/// column, as n_distance searches (search/incremental/min_distance.hpp), with the stratum's rules as edges: per goal
/// key, A* (search/a_star.hpp) from the key's sources, estimating with the maximum of the GoalHeuristic, the distances
/// learned by earlier searches (Generalized Adaptive A*, Sun, Koenig & Yeoh 2008) and the bound of a backward frontier
/// (Pohl's cardinality comparison), ending on a stored path that still holds (Multipath GAA*, Hernández, Asín & Baier
/// 2015). Per goal key the search tree and the backward frontier are kept while the key's targets stay the same:
/// removed edges keep the learned distances admissible, and the edges that an update adds (the rules run with a changed
/// literal reading its change) are repaired (TreeRepair) or restart the frontier; a change these rules cannot express
/// discards the trees. A search derives only the row of the target it reaches, at the distance: the goals read nothing
/// else.
class UnitSearch
{
public:
    /// None unless the stratum has that shape and every rule can be reversed with all its literals.
    static std::unique_ptr<UnitSearch> make(CompiledProgram& program, const StratumInfo& stratum, const GoalSearch& search);
    UnitSearch(const UnitSearch&) = delete;
    UnitSearch& operator=(const UnitSearch&) = delete;
    UnitSearch(UnitSearch&&) = delete;
    UnitSearch& operator=(UnitSearch&&) = delete;
    ~UnitSearch();

    /// Discards the stored searches (the inputs were not changed incrementally).
    void reset() noexcept;
    /// Records the edges that the changes of the inputs (their added and removed rows) add.
    void changed(CompiledProgram& program, Executor& executor);
    /// Searches each goal key from its sources (rows of R derived by the base rules) and derives the reached target's
    /// row into R's current rows. `heuristic` estimates for the current targets, or is null.
    /// `targets_changed` false: the targets are those of the last run.
    void run(CompiledProgram& program, Executor& executor, std::span<const std::vector<uint_t>> sources, GoalHeuristic* heuristic, bool targets_changed);
    [[nodiscard]] size_t memory_usage() const noexcept;

private:
    struct Key;
    struct Oracle;
    /// A rule run with one literal reading its change, deriving the added edges: the read row's key, then the head's,
    /// each in the key columns `columns`. Goal key columns that the rule leaves unbound are missing: the rules keep the
    /// goal key, so the edge exists for every goal key.
    struct Delta
    {
        CompiledRule rule;
        size_t literal;
        Version change;
        std::vector<size_t> columns;
    };
    GoalSearch m_search;
    std::vector<size_t> m_goal_columns;
    /// The recursive rules without their read of R, run from a key at weight 0, and reversed; `m_edges` are the
    /// forward rules run with the head's key bound too, which check one edge.
    std::vector<BoundRule> m_forward;
    std::vector<BoundRule> m_edges;
    std::vector<BoundRule> m_reversed;
    std::vector<Delta> m_deltas;
    /// The tables whose changes the delta rules cannot express.
    std::vector<size_t> m_opaque;
    RawArraySet<uint_t> m_goal_keys;
    std::vector<std::unique_ptr<Key>> m_keys;
    /// Per goal key, its targets as bytes, from the last run that read them.
    std::vector<std::vector<std::vector<std::byte>>> m_key_targets;
    bool m_read = false;
    uint64_t m_version = 0;
    std::vector<Version> m_reads;
    std::vector<std::vector<uint_t>> m_derived;
    std::vector<uint_t> m_key;

    UnitSearch(const GoalSearch& search, std::vector<BoundRule> forward, std::vector<BoundRule> edges, std::vector<BoundRule> reversed);
    std::span<const uint_t> goal_key(std::span<const uint_t> key);
    /// The index of the goal key, with its search made on first use.
    uint_t key_search(std::span<const uint_t> goal_key, size_t vertex_size);
};
}  // namespace ygg::database::datalog::detail

#endif
