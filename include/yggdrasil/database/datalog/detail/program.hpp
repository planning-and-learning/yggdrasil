/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_DATALOG_DETAIL_PROGRAM_HPP_
#define YGG_DATABASE_DATALOG_DETAIL_PROGRAM_HPP_

#include "yggdrasil/database/datalog/detail/strata.hpp"
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
        /// Terms lhs, rhs and the result variable, which it binds (or checks, if a plan binds it earlier).
        ARITHMETIC,
    };
    Kind kind;
    size_t table = npos;
    /// A variable's term holds its slot in the rule.
    std::vector<Term> terms;
    Comparison::Kind comparison = Comparison::Kind::EQUAL;
    Arithmetic::Operation operation = Arithmetic::Operation::ADD;
};

/// Whether the literal reads a table, so that a plan can start with it.
inline bool is_generator_kind(Literal::Kind kind) { return kind != Literal::Kind::COMPARISON && kind != Literal::Kind::ARITHMETIC; }

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

struct CompiledProgram
{
    std::vector<std::unique_ptr<Table>> tables;
    std::vector<CompiledRule> rules;
    /// In evaluation order; only those the outputs depend on.
    std::vector<std::unique_ptr<Stratum>> strata;
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
