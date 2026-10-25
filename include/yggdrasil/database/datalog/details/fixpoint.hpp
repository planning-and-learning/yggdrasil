/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_DATALOG_DETAILS_FIXPOINT_HPP_
#define YGG_DATABASE_DATALOG_DETAILS_FIXPOINT_HPP_

#include "yggdrasil/database/datalog/details/executor.hpp"
#include "yggdrasil/database/datalog/details/program.hpp"

#include <span>

/// Evaluation and maintenance of one stratum: semi-naive evaluation (seminaive.cpp), counting for strata without
/// recursion (counting.cpp), Delete and Rederive for recursive ones (dred.cpp), and aggregates (aggregation.cpp).
namespace ygg::database::datalog::detail
{
void evaluate_plain(CompiledProgram& program, Executor& executor, const StratumInfo& stratum, bool counting);
void update_plain(CompiledProgram& program, Executor& executor, const StratumInfo& stratum);
void evaluate_recursive(CompiledProgram& program, Executor& executor, const StratumInfo& stratum);
/// The stratum's relations from their rules alone, into empty tables.
void derive_recursive(CompiledProgram& program, Executor& executor, const StratumInfo& stratum);
void update_recursive(CompiledProgram& program, Executor& executor, const StratumInfo& stratum);
void evaluate_aggregate(CompiledProgram& program, const StratumInfo& stratum);
void update_aggregate(CompiledProgram& program, const StratumInfo& stratum);

/// Moves the staged rows of the stratum's tables into the frontier; `add` decides each row's fate.
template<typename Add>
bool merge(CompiledProgram& program, const StratumInfo& stratum, const Add& add)
{
    bool any = false;
    for (const auto t : stratum.tables)
    {
        auto& table = *program.tables[t];
        table.frontier.clear();
        table.next.for_each(
            [&](std::span<const uint_t> row)
            {
                add(table, row);
                table.frontier.insert(row);
            });
        any = any || !table.next.empty();
        table.next.clear();
    }
    return any;
}

/// Semi-naive iterations from the frontier: each rule once per literal of the stratum, which reads the frontier,
/// the others `others`; rows that `keep` accepts are staged and merged by `add`.
template<typename Keep, typename Add>
void iterate(CompiledProgram& program, Executor& executor, const StratumInfo& stratum, Version others, const Keep& keep, const Add& add)
{
    while (true)
    {
        for (const auto r : stratum.rules)
        {
            const auto& rule = program.rules[r];
            auto& head = *program.tables[rule.head];
            for (size_t j = 0; j < rule.literals.size(); ++j)
            {
                const auto& literal = rule.literals[j];
                if (literal.kind != Literal::Kind::POSITIVE || !stratum.contains(literal.table) || program.tables[literal.table]->frontier.empty())
                    continue;
                executor.set_versions(rule, others);
                executor.set_version(j, Version::FRONTIER);
                executor.run(rule,
                             rule.by_source[j],
                             [&](std::span<const uint_t> row)
                             {
                                 if (keep(head, row))
                                     head.next.insert(row);
                             });
            }
        }
        if (!merge(program, stratum, add))
            return;
    }
}
}  // namespace ygg::database::datalog::detail

#endif
