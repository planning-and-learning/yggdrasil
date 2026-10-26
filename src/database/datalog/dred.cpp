/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/datalog/details/fixpoint.hpp"

/// Recursive strata are maintained by Delete and Rederive [Gupta93].
namespace ygg::database::datalog::detail
{
namespace
{
/// Past this fraction of the stratum's rows over-deleted, rederiving costs more than evaluating the stratum anew.
constexpr double recompute_fraction = 0.1;

/// Runs every rule once per changed literal outside the stratum whose change set `set(kind)` names, the others
/// reading `others` (the stratum's own relations their current rows), and stages the rows `keep` accepts.
template<typename Set, typename Keep>
void seed(CompiledProgram& program, Executor& executor, const StratumInfo& stratum, Version others, const Set& set, const Keep& keep)
{
    for (const auto r : stratum.rules)
    {
        const auto& rule = program.rules[r];
        auto& head = *program.tables[rule.head];
        for (size_t j = 0; j < rule.literals.size(); ++j)
        {
            const auto& literal = rule.literals[j];
            if (literal.table == npos || stratum.contains(literal.table) || !program.tables[literal.table]->changed())
                continue;
            executor.set_versions(rule, others);
            for (size_t i = 0; i < rule.literals.size(); ++i)
                if (rule.literals[i].table != npos && stratum.contains(rule.literals[i].table))
                    executor.set_version(i, Version::CURRENT);
            executor.set_version(j, set(literal.kind));
            executor.run(rule,
                         rule.by_source[j],
                         [&](std::span<const uint_t> row)
                         {
                             if (keep(head, row))
                                 head.next.insert(row);
                         });
        }
    }
}

bool overdeleted_too_many(const CompiledProgram& program, const StratumInfo& stratum)
{
    size_t deleted = 0, rows = 0;
    for (const auto t : stratum.tables)
    {
        deleted += program.tables[t]->removed.size();
        rows += program.tables[t]->current.size();
    }
    return static_cast<double>(deleted) > recompute_fraction * static_cast<double>(rows);
}

/// The over-deletion's semi-naive iterations: a literal of the stratum reads the rows deleted last, the others the
/// old relations. Returns false once too many rows are deleted.
template<typename Keep, typename Add>
bool iterate_deletions(CompiledProgram& program, Executor& executor, const StratumInfo& stratum, const Keep& keep, const Add& add)
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
                executor.set_versions(rule, Version::OLD);
                for (size_t i = 0; i < rule.literals.size(); ++i)
                    if (rule.literals[i].table != npos && stratum.contains(rule.literals[i].table))
                        executor.set_version(i, Version::CURRENT);
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
            return true;
        if (overdeleted_too_many(program, stratum))
            return false;
    }
}

/// Evaluates the stratum anew and records the difference to its old rows as its changes.
void recompute(CompiledProgram& program, Executor& executor, const StratumInfo& stratum)
{
    for (const auto t : stratum.tables)
    {
        auto& table = *program.tables[t];
        table.removed.clear();
        table.snapshot.clear();
        table.current.for_each([&](std::span<const uint_t> row) { table.snapshot.insert(row); });
        for (auto* set : { &table.current, &table.frontier, &table.next })
            set->clear();
    }
    derive_recursive(program, executor, stratum);
    for (const auto t : stratum.tables)
    {
        auto& table = *program.tables[t];
        table.current.for_each(
            [&](std::span<const uint_t> row)
            {
                if (!table.snapshot.contains(row))
                    table.added.insert(row);
            });
        for (uint_t i = 0; i < table.snapshot.size(); ++i)
            if (!table.current.contains((table.snapshot)[i]))
                table.removed.insert((table.snapshot)[i]);
        table.snapshot.clear();
    }
}
}  // namespace

void update_recursive(CompiledProgram& program, Executor& executor, const StratumInfo& stratum)
{
    // 1. Over-delete: rows with a derivation through a removed row, with the old relations; the stratum's own
    //    relations are still the old ones.
    const auto deletable = [&](Table& table, std::span<const uint_t> row) { return table.current.contains(row) && !table.removed.contains(row); };
    const auto deleted = [&](Table& table, std::span<const uint_t> row) { table.removed.insert(row); };
    seed(
        program,
        executor,
        stratum,
        Version::OLD,
        [](Literal::Kind kind) { return kind == Literal::Kind::NEGATED ? Version::ADDED : Version::REMOVED; },
        deletable);
    merge(program, stratum, deleted);
    if (overdeleted_too_many(program, stratum) || !iterate_deletions(program, executor, stratum, deletable, deleted))
        return recompute(program, executor, stratum);
    // 2. Remove them.
    for (const auto t : stratum.tables)
    {
        auto& table = *program.tables[t];
        table.removed.for_each([&](std::span<const uint_t> row) { table.current.erase(row); });
        table.frontier.clear();
    }
    // 3. Rederive the deleted rows that a rule still derives from the remaining rows.
    for (const auto r : stratum.rules)
    {
        const auto& rule = program.rules[r];
        auto& head = *program.tables[rule.head];
        executor.set_versions(rule, Version::CURRENT);
        head.removed.for_each(
            [&](std::span<const uint_t> row)
            {
                if (!head.next.contains(row) && executor.derives(rule, rule.rederive, row))
                    head.next.insert(row);
            });
    }
    // 4. Insert semi-naively from the rederived rows and the additions.
    const auto inserted = [&](Table& table, std::span<const uint_t> row)
    {
        table.current.insert(row);
        if (!table.removed.erase(row))
            table.added.insert(row);
    };
    const auto absent = [&](Table& table, std::span<const uint_t> row) { return !table.current.contains(row) && !table.next.contains(row); };
    for (const auto t : stratum.tables)
        program.tables[t]->next.for_each([&](std::span<const uint_t> row) { program.tables[t]->current.insert(row); });
    for (const auto t : stratum.tables)
    {
        auto& table = *program.tables[t];
        table.next.for_each(
            [&](std::span<const uint_t> row)
            {
                table.removed.erase(row);
                table.frontier.insert(row);
            });
        table.next.clear();
    }
    seed(
        program,
        executor,
        stratum,
        Version::CURRENT,
        [](Literal::Kind kind) { return kind == Literal::Kind::NEGATED ? Version::REMOVED : Version::ADDED; },
        absent);
    for (const auto t : stratum.tables)
    {
        auto& table = *program.tables[t];
        table.next.for_each(
            [&](std::span<const uint_t> row)
            {
                inserted(table, row);
                table.frontier.insert(row);
            });
        table.next.clear();
    }
    iterate(program, executor, stratum, Version::CURRENT, absent, inserted);
    for (const auto t : stratum.tables)
        program.tables[t]->frontier.clear();
}
}  // namespace ygg::database::datalog::detail
