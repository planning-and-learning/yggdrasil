/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_DATALOG_DETAIL_PROGRAM_HPP_
#define YGG_DATABASE_DATALOG_DETAIL_PROGRAM_HPP_

#include "yggdrasil/database/datalog/detail/table.hpp"
#include "yggdrasil/database/datalog/repository.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

/// The compiled form of a program that the evaluator runs: tables, rules with their plans, aggregates and strata.
namespace ygg::database::datalog::detail
{
struct Literal
{
    enum class Kind : uint8_t
    {
        POSITIVE,
        NEGATED,
        /// The aggregate's table, read with the group's terms followed by the result variable.
        AGGREGATE,
        COMPARISON,
    };
    Kind kind;
    size_t table = npos;
    /// A variable's term holds its slot in the rule.
    std::vector<Term> terms;
    Comparison::Kind comparison = Comparison::Kind::EQUAL;
};

inline bool is_generator_kind(Literal::Kind kind) { return kind != Literal::Kind::COMPARISON; }

/// One literal of a plan: a generator enumerates the rows of its table that match the bound terms (an index lookup,
/// or a scan without bound terms) and binds the others; a filter tests a fully bound literal.
struct Step
{
    size_t literal;
    bool generator;
    size_t index = npos;
    Step(size_t literal_, bool generator_) : literal(literal_), generator(generator_) {}
    std::vector<Term> key;
    std::vector<std::pair<size_t, uint_t>> binds;
    std::vector<std::pair<size_t, uint_t>> checks;
    /// The literals whose only unbound variables are the ones this generator binds: at run time the one with the
    /// fewest matches enumerates and the others are checked, Generic Join's intersection [Ngo13], so that cyclic bodies
    /// stay worst-case optimal.
    std::vector<Step> peers;
};

struct Plan
{
    std::vector<Step> steps;
};

struct CompiledRule
{
    size_t head;
    std::vector<Term> head_terms;
    std::vector<Literal> literals;
    size_t slots = 0;
    /// Per literal, the plan that starts with it as generator (empty for comparisons); with no source; with the head
    /// bound.
    std::vector<Plan> by_source;
    Plan plain;
    Plan rederive;
};

/// The groups of an aggregate's table, kept from its body's table (its satisfying assignments).
struct AggregateTable
{
    size_t table = npos;
    size_t source = npos;
    AggregateFunction function = AggregateFunction::COUNT;
    std::vector<size_t> group;
    size_t value = 0;
    struct Group
    {
        size_t count = 0;
        uint64_t sum = 0;
        std::map<uint_t, size_t> values;
    };
    std::unique_ptr<RawArraySet<uint_t>> groups;
    std::vector<Group> states;

    [[nodiscard]] std::optional<uint_t> value_of(const Group& state) const
    {
        if (state.count == 0)
            return std::nullopt;
        switch (function)
        {
            case AggregateFunction::COUNT:
                return to_uint_t(state.count);
            case AggregateFunction::SUM:
                return static_cast<uint_t>(state.sum);
            case AggregateFunction::MIN:
                return state.values.begin()->first;
            case AggregateFunction::MAX:
                return state.values.rbegin()->first;
        }
        return std::nullopt;
    }
};

struct StratumInfo
{
    enum class Kind : uint8_t
    {
        AGGREGATE,
        PLAIN,
        RECURSIVE,
    };
    Kind kind;
    std::vector<size_t> tables;
    std::vector<size_t> rules;
    /// The tables its rules read (for an aggregate, its body's table).
    std::vector<size_t> reads;
    size_t aggregate = npos;

    [[nodiscard]] bool contains(size_t table) const { return std::ranges::find(tables, table) != tables.end(); }
};

struct CompiledProgram
{
    std::vector<std::unique_ptr<Table>> tables;
    std::vector<CompiledRule> rules;
    std::vector<AggregateTable> aggregates;
    /// In evaluation order; only those the outputs depend on.
    std::vector<StratumInfo> strata;
    /// Per input slot its table, npos if no output depends on it.
    std::vector<size_t> slot_tables;
    std::vector<size_t> outputs;
    /// Per table, whether it is a relation of a recursive stratum.
    std::vector<bool> recursive_tables;
};

/// Translates the program (translation.cpp) and plans its rules (planner.cpp).
CompiledProgram translate(ProgramView view, std::span<const RelationView> outputs, std::span<const size_t> static_slots);
void plan_rules(CompiledProgram& program);
}  // namespace ygg::database::datalog::detail

#endif
