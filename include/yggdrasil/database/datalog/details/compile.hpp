/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_DATALOG_DETAILS_COMPILE_HPP_
#define YGG_DATABASE_DATALOG_DETAILS_COMPILE_HPP_

#include "yggdrasil/database/datalog/repository.hpp"
#include "yggdrasil/database/syntax/query.hpp"

#include <cstddef>
#include <span>

namespace ygg::database::datalog::detail
{
/// A program as a query DAG: root i is outputs[i], with columns labeled 0, ..., arity − 1. Input slot s of the program is
/// query input slot s; `unit_slot`, past the program's slots, reads a relation without columns holding one row, the
/// groups of an aggregate in a rule without positive atoms.
struct QueryProgram
{
    QueryPlan plan;
    size_t unit_slot = 0;
};

/// Each derived relation is the union of its rules; a rule is the natural join of its positive atoms (constants and
/// repeated variables as selections), joined with its aggregates, restricted by its negated atoms (antijoins) and
/// comparisons, and projected onto its head. An aggregate aggregates its own body, grouped by the variables the rule's
/// positive atoms bind. Only the relations the outputs depend on are compiled.
QueryProgram compile(ProgramView program, std::span<const RelationView> outputs);
}  // namespace ygg::database::datalog::detail

#endif
