/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/datalog/details/fixpoint.hpp"

#include <stdexcept>

/// Strata without recursion keep the number of derivations of each row [Gupta93, counting].
namespace ygg::database::datalog::detail
{
namespace
{
void count(Table& table, std::span<const uint_t> row, int64_t change)
{
    const auto position = table.pending_rows.insert(row);
    if (position == table.pending.size())
        table.pending.push_back(0);
    table.pending[position] += change;
}

/// Applies the pending count changes; a row whose count leaves or reaches 0 is added or removed.
void apply_counts(Table& table, bool record)
{
    for (uint_t p = 0; p < table.pending_rows.size(); ++p)
    {
        const auto change = table.pending[p];
        if (change == 0)
            continue;
        const auto row = (table.pending_rows)[p];
        const auto position = table.count_rows.find(row);
        const auto before = position ? table.counts[*position] : 0;
        const auto after = before + change;
        if (after < 0)
            throw std::logic_error("Datalog evaluator: a derivation count became negative.");
        if (position && after == 0)
        {
            table.counts[*position] = table.counts.back();
            table.counts.pop_back();
            table.count_rows.erase(*position);
        }
        else if (position)
            table.counts[*position] = after;
        else
        {
            table.count_rows.insert(row);
            table.counts.push_back(after);
        }
        if (before == 0 && after > 0)
        {
            table.current.insert(row);
            if (record)
                table.added.insert(row);
        }
        else if (before > 0 && after == 0)
        {
            table.current.erase(row);
            if (record)
                table.removed.insert(row);
        }
    }
    table.pending_rows.clear();
    table.pending.clear();
}
}  // namespace

void evaluate_plain(CompiledProgram& program, Executor& executor, const StratumInfo& stratum, bool counting)
{
    for (const auto table : stratum.tables)
        program.tables[table]->clear();
    for (const auto r : stratum.rules)
    {
        const auto& rule = program.rules[r];
        auto& head = *program.tables[rule.head];
        executor.set_versions(rule, Version::CURRENT);
        executor.run(rule,
                     rule.plain,
                     [&](std::span<const uint_t> row)
                     {
                         if (counting)
                             count(head, row, 1);
                         else
                             head.current.insert(row);
                     });
        if (counting)
            apply_counts(head, false);
    }
}

/// The rule once per changed literal, which reads its added (or removed) rows, earlier literals the new, later ones the
/// old relations; a negated literal's additions remove derivations.
void update_plain(CompiledProgram& program, Executor& executor, const StratumInfo& stratum)
{
    for (const auto r : stratum.rules)
    {
        const auto& rule = program.rules[r];
        auto& head = *program.tables[rule.head];
        for (size_t j = 0; j < rule.literals.size(); ++j)
        {
            const auto& literal = rule.literals[j];
            if (literal.table == npos || !program.tables[literal.table]->changed())
                continue;
            for (const auto change : { Version::ADDED, Version::REMOVED })
            {
                const bool positive = literal.kind != Literal::Kind::NEGATED;
                const int64_t sign = (change == Version::ADDED) == positive ? 1 : -1;
                executor.set_versions(rule, Version::CURRENT);
                for (size_t i = j + 1; i < rule.literals.size(); ++i)
                    executor.set_version(i, Version::OLD);
                executor.set_version(j, change);
                executor.run(rule, rule.by_source[j], [&](std::span<const uint_t> row) { count(head, row, sign); });
            }
        }
    }
    for (const auto table : stratum.tables)
        apply_counts(*program.tables[table], true);
}
}  // namespace ygg::database::datalog::detail
