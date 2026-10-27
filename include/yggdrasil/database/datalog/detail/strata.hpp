/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_DATALOG_DETAIL_STRATA_HPP_
#define YGG_DATABASE_DATALOG_DETAIL_STRATA_HPP_

#include "yggdrasil/database/datalog/detail/table.hpp"
#include "yggdrasil/database/datalog/repository.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace ygg::database::datalog::detail
{
struct CompiledProgram;
struct CompiledRule;
class Executor;
class GoalHeuristic;

/// The relations of a stratum, the rules that derive them, and the tables they read.
struct StratumInfo
{
    std::vector<size_t> tables;
    std::vector<size_t> rules;
    /// The tables its rules read (for an aggregate, its body's table).
    std::vector<size_t> reads;
    [[nodiscard]] bool contains(size_t table) const { return std::ranges::find(tables, table) != tables.end(); }
    /// The position of one of its tables in `tables`.
    [[nodiscard]] size_t position(size_t table) const { return static_cast<size_t>(std::ranges::find(tables, table) - tables.begin()); }
};

/// One stratum of a program, evaluated after the strata it reads. Its tables' `current` rows are its relations; an
/// update reads the changes (`added`, `removed`) of the tables it reads and records the changes of its own.
class Stratum
{
public:
    explicit Stratum(StratumInfo info) : m_info(std::move(info)) {}
    Stratum(const Stratum&) = delete;
    Stratum& operator=(const Stratum&) = delete;
    Stratum(Stratum&&) = delete;
    Stratum& operator=(Stratum&&) = delete;
    virtual ~Stratum() = default;

    [[nodiscard]] const StratumInfo& info() const noexcept { return m_info; }
    /// Its relations from the current rows of the tables it reads, without keeping what update() needs.
    virtual void evaluate(CompiledProgram& program, Executor& executor) = 0;
    /// As evaluate(), keeping what update() needs.
    virtual void initialize(CompiledProgram& program, Executor& executor) { evaluate(program, executor); }
    /// Applies the changes of the tables it reads, after initialize().
    virtual void update(CompiledProgram& program, Executor& executor) = 0;
    [[nodiscard]] size_t memory_usage() const noexcept { return m_reads.capacity() * sizeof(Version) + state_memory_usage(); }

protected:
    /// Every literal of the rule reads `all`; the caller may change single literals before the run.
    std::vector<Version>& reads(const CompiledRule& rule, Version all);

private:
    StratumInfo m_info;
    /// What each literal of a rule reads in the next run, rebuilt before each run.
    std::vector<Version> m_reads;

    /// The memory of what the stratum maintains.
    [[nodiscard]] virtual size_t state_memory_usage() const noexcept = 0;
};

/// A stratum without recursion: one pass over its rules, and for updates the derivation count of each row, changed by
/// the rule with one changed literal reading its added or removed rows, earlier literals the new and later ones the old
/// relations [Gupta93, counting] (plain.cpp).
class PlainStratum final : public Stratum
{
public:
    PlainStratum(StratumInfo stratum, const CompiledProgram& program);

    void evaluate(CompiledProgram& program, Executor& executor) override;
    void initialize(CompiledProgram& program, Executor& executor) override;
    void update(CompiledProgram& program, Executor& executor) override;

private:
    [[nodiscard]] size_t state_memory_usage() const noexcept override;

    /// Per table of the stratum, the derivation count of each row and the changes staged by one update.
    struct Counts
    {
        RawArraySet<uint_t> rows;
        std::vector<int64_t> counts;
        RawArraySet<uint_t> pending_rows;
        std::vector<int64_t> pending;
        explicit Counts(size_t arity) : rows(arity), pending_rows(arity) {}
    };
    std::vector<Counts> m_counts;

    void derive(CompiledProgram& program, Executor& executor, bool counting);
    void count(size_t table, std::span<const uint_t> row, int64_t change);
    /// Applies the staged count changes; a row whose count leaves or reaches 0 is added or removed (and, if `record`,
    /// recorded as a change).
    void apply_counts(Table& table, Counts& counts, bool record);
};

/// A recursive stratum: semi-naive evaluation, and Delete and Rederive for updates [Gupta93]: delete every row with a
/// derivation through a removed row (with the old relations), rederive the deleted rows that the remaining rows still
/// derive (the rule with its head bound), then insert semi-naively from the additions and the rederived rows
/// (recursive.cpp).
class RecursiveStratum final : public Stratum
{
public:
    RecursiveStratum(StratumInfo stratum, const CompiledProgram& program);

    void evaluate(CompiledProgram& program, Executor& executor) override;
    void update(CompiledProgram& program, Executor& executor) override;

private:
    [[nodiscard]] size_t state_memory_usage() const noexcept override;

    /// Per table of the stratum: the rows found by the current semi-naive iteration, and its rows before it is
    /// evaluated anew.
    std::vector<IndexedTuples> m_next;
    std::vector<RawArraySet<uint_t>> m_snapshot;

    /// The relations from the rules alone, into empty tables.
    void derive(CompiledProgram& program, Executor& executor);
    /// Moves the rows found last into the frontier; `add` decides each row's fate. Whether any row was found.
    template<typename Add>
    bool merge(CompiledProgram& program, const Add& add);
    /// Semi-naive iterations from the frontier: each rule once per literal of the stratum, which reads the frontier,
    /// the others `others`; rows that `keep(table, staged, row)` accepts are staged and merged by `add`.
    template<typename Keep, typename Add>
    void iterate(CompiledProgram& program, Executor& executor, Version others, const Keep& keep, const Add& add);
    /// Every rule once per changed literal outside the stratum, which reads `change(kind)`, the stratum's own relations
    /// their current rows and the others `others`; stages the rows `keep` accepts.
    template<typename Change, typename Keep>
    void seed(CompiledProgram& program, Executor& executor, Version others, const Change& change, const Keep& keep);
    /// The over-deletion's semi-naive iterations: a literal of the stratum reads the rows deleted last, the others the
    /// old relations. False once too many rows are deleted.
    template<typename Keep, typename Add>
    bool iterate_deletions(CompiledProgram& program, Executor& executor, const Keep& keep, const Add& add);
    [[nodiscard]] bool overdeleted_too_many(const CompiledProgram& program) const;
    /// Evaluates the stratum anew and records the difference to its old rows as its changes.
    void recompute(CompiledProgram& program, Executor& executor);
};

/// A stratum of weighted relations: each keeps, per row of its other columns, the least weight derivable by rules whose
/// head weight is the sum or maximum of their weighted atoms' weights and other terms. Evaluated by Knuth's lightest
/// derivation algorithm (Felzenszwalb & McAllester 2007, §3): rows are settled in order of weight from a priority queue,
/// each settled row expanded by the rules that read it with the settled rows (weighted.cpp). If only goal rules read the
/// stratum (a min head over one of its relations and the targets), it stops once every goal holds: the first settled
/// row a goal reads has the least weight. A goal with key columns holds per key, and the rows of a key whose goal holds
/// are no longer queued or settled (the rules keep the key). With a heuristic derived from its rules (heuristic.hpp), rows
/// settle in order of weight plus the heuristic (A*, Felzenszwalb & McAllester 2007, §4), which keeps that order
/// correct since the heuristic is monotone. An update evaluates the stratum anew and records the difference to its
/// old rows.
class WeightedStratum final : public Stratum
{
public:
    /// `goals`: the rules of higher strata that read the stratum, if all of them are goals; empty, the stratum is
    /// evaluated completely.
    WeightedStratum(StratumInfo stratum, const CompiledProgram& program, std::vector<size_t> goals, std::unique_ptr<GoalHeuristic> heuristic);
    WeightedStratum(const WeightedStratum&) = delete;
    WeightedStratum& operator=(const WeightedStratum&) = delete;
    WeightedStratum(WeightedStratum&&) = delete;
    WeightedStratum& operator=(WeightedStratum&&) = delete;
    ~WeightedStratum() override;

    void evaluate(CompiledProgram& program, Executor& executor) override;
    void update(CompiledProgram& program, Executor& executor) override;

private:
    [[nodiscard]] size_t state_memory_usage() const noexcept override;

    /// A derived row of a table of the stratum, waiting to be settled, by its weight plus the heuristic.
    struct Candidate
    {
        uint_t priority;
        size_t position;
        std::vector<uint_t> row;
        friend bool operator>(const Candidate& lhs, const Candidate& rhs) noexcept { return lhs.priority > rhs.priority; }
    };
    /// Per table of the stratum: the rows of the other columns settled, and the least weight queued per such row.
    std::vector<RawArraySet<uint_t>> m_settled;
    std::vector<RawArraySet<uint_t>> m_queued;
    std::vector<std::vector<uint_t>> m_best;
    std::vector<Candidate> m_queue;
    std::vector<RawArraySet<uint_t>> m_snapshot;
    std::vector<size_t> m_goals;
    /// Per goal without key columns, whether it holds.
    std::vector<bool> m_holds;
    /// Per goal, the table of the stratum it reads, the columns of the read row that are its key, and the keys whose
    /// goal holds.
    struct GoalKeys
    {
        size_t position;
        std::vector<size_t> columns;
        RawArraySet<uint_t> done;
    };
    std::vector<GoalKeys> m_goal_keys;
    std::vector<uint_t> m_key;
    std::unique_ptr<GoalHeuristic> m_heuristic;
    /// The rows a run derived, offered after it.
    std::vector<std::pair<size_t, std::vector<uint_t>>> m_derived;

    /// Records the goals that the settled row (the frontier of its table) makes hold; whether every goal holds.
    bool goals_hold(CompiledProgram& program, Executor& executor, size_t table);
    /// Whether the goals that read the table at `position` hold for the row's keys; false for goals without keys.
    [[nodiscard]] bool finished(size_t position, std::span<const uint_t> row);
    /// Queues a derived row of the stratum's table at `position` unless a lighter one is settled or queued, or the
    /// heuristic shows that it reaches no target.
    void offer(size_t position, std::span<const uint_t> row);
    void offer_derived();
    void derive(CompiledProgram& program, Executor& executor);
};

/// The groups of an aggregate, kept from its body's table (its satisfying assignments) (aggregate.cpp).
struct AggregateTable
{
    size_t table = npos;
    size_t source = npos;
    AggregateFunction function = AggregateFunction::COUNT;
    std::vector<size_t> group;
    size_t value = 0;
};

/// The stratum of one aggregate: per group of its body's rows, the count, sum and value counts, from which its table
/// holds one row per group with its value; updates change the groups of the body's changed rows.
class AggregateStratum final : public Stratum
{
public:
    AggregateStratum(StratumInfo stratum, AggregateTable aggregate);

    void evaluate(CompiledProgram& program, Executor& executor) override;
    void update(CompiledProgram& program, Executor& executor) override;

private:
    [[nodiscard]] size_t state_memory_usage() const noexcept override;

    struct Group
    {
        size_t count = 0;
        uint64_t sum = 0;
        std::map<uint_t, size_t> values;
    };
    AggregateTable m_aggregate;
    RawArraySet<uint_t> m_groups;
    std::vector<Group> m_states;
    std::vector<uint_t> m_key;
    std::vector<uint_t> m_row;

    [[nodiscard]] std::optional<uint_t> value_of(const Group& state) const;
    /// The state of the group of a body row, created if absent.
    std::pair<uint_t, Group&> group_of(std::span<const uint_t> row);
    void change_group(Group& state, std::span<const uint_t> row, bool add) const;
    std::span<const uint_t> group_row(std::span<const uint_t> group, uint_t value);
};
}  // namespace ygg::database::datalog::detail

#endif
