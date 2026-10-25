/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/datalog/details/fixpoint.hpp"

#include <algorithm>

namespace ygg::database::datalog::detail
{
void evaluate_recursive(CompiledProgram& program, Executor& executor, const StratumInfo& stratum)
{
    for (const auto table : stratum.tables)
        program.tables[table]->clear();
    derive_recursive(program, executor, stratum);
}

/// The rules that read no relation of the stratum, then semi-naively.
void derive_recursive(CompiledProgram& program, Executor& executor, const StratumInfo& stratum)
{
    for (const auto r : stratum.rules)
    {
        const auto& rule = program.rules[r];
        auto& head = *program.tables[rule.head];
        if (std::ranges::any_of(rule.literals, [&](const Literal& l) { return l.kind == Literal::Kind::POSITIVE && stratum.contains(l.table); }))
            continue;
        executor.set_versions(rule, Version::NEW);
        executor.run(rule, rule.plain, [&](std::span<const uint_t> row) { head.next.insert(row); });
    }
    const auto add = [&](Table& table, std::span<const uint_t> row) { table.current.insert(row); };
    const auto absent = [&](Table& table, std::span<const uint_t> row) { return !table.current.contains(row); };
    merge(program, stratum, add);
    iterate(program, executor, stratum, Version::NEW, absent, add);
    for (const auto table : stratum.tables)
        program.tables[table]->frontier.clear();
}
}  // namespace ygg::database::datalog::detail
