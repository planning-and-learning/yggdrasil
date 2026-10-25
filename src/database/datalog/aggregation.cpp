/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/datalog/details/fixpoint.hpp"

#include <optional>
#include <set>
#include <utility>
#include <vector>

/// An aggregate's table holds a row (group, value) per group of its body's table; absent groups read as the fallback.
namespace ygg::database::datalog::detail
{
namespace
{
std::vector<uint_t> group_key(const AggregateTable& aggregate, std::span<const uint_t> row)
{
    auto key = std::vector<uint_t>();
    for (const auto column : aggregate.group)
        key.push_back(row[column]);
    return key;
}

void change_group(AggregateTable& aggregate, AggregateTable::Group& state, std::span<const uint_t> row, bool add)
{
    state.count = add ? state.count + 1 : state.count - 1;
    if (aggregate.function == AggregateFunction::COUNT)
        return;
    const auto value = row[aggregate.value];
    if (aggregate.function == AggregateFunction::SUM)
        state.sum = add ? state.sum + value : state.sum - value;
    else if (add)
        ++state.values[value];
    else if (const auto it = state.values.find(value); --it->second == 0)
        state.values.erase(it);
}

std::span<const uint_t> group_row(std::vector<uint_t>& out, std::span<const uint_t> group, uint_t value)
{
    out.assign(group.begin(), group.end());
    out.push_back(value);
    return out;
}
}  // namespace

void evaluate_aggregate(CompiledProgram& program, const StratumInfo& stratum)
{
    auto& aggregate = program.aggregates[stratum.aggregate];
    auto& table = *program.tables[aggregate.table];
    table.clear();
    aggregate.groups->clear();
    aggregate.states.clear();
    program.tables[aggregate.source]->current.for_each(
        [&](std::span<const uint_t> row)
        {
            const auto [position, inserted] = aggregate.groups->insert(group_key(aggregate, row));
            if (inserted)
                aggregate.states.emplace_back();
            change_group(aggregate, aggregate.states[position], row, true);
        });
    auto row = std::vector<uint_t>();
    for (uint_t g = 0; g < aggregate.groups->size(); ++g)
        if (const auto value = aggregate.value_of(aggregate.states[g]))
            table.current.insert(group_row(row, (*aggregate.groups)[g], *value));
}

void update_aggregate(CompiledProgram& program, const StratumInfo& stratum)
{
    auto& aggregate = program.aggregates[stratum.aggregate];
    auto& table = *program.tables[aggregate.table];
    auto& source = *program.tables[aggregate.source];
    auto touched = std::vector<std::pair<uint_t, std::optional<uint_t>>>();
    auto is_touched = std::set<uint_t>();
    const auto apply = [&](std::span<const uint_t> row, bool add)
    {
        const auto [position, inserted] = aggregate.groups->insert(group_key(aggregate, row));
        if (inserted)
            aggregate.states.emplace_back();
        if (is_touched.insert(position).second)
            touched.emplace_back(position, aggregate.value_of(aggregate.states[position]));
        change_group(aggregate, aggregate.states[position], row, add);
    };
    source.removed.for_each([&](std::span<const uint_t> row) { apply(row, false); });
    source.added.for_each([&](std::span<const uint_t> row) { apply(row, true); });
    auto row = std::vector<uint_t>();
    for (const auto& [g, before] : touched)
    {
        const auto after = aggregate.value_of(aggregate.states[g]);
        const auto group = (*aggregate.groups)[g];
        if (before)
            table.current.erase(group_row(row, group, *before));
        if (after)
            table.current.insert(group_row(row, group, *after));
        // An absent group reads as the fallback value, so the change sets hold it in place of the absent row.
        const auto old_value = before ? before : table.fallback;
        const auto new_value = after ? after : table.fallback;
        if (old_value == new_value)
            continue;
        if (old_value)
            table.removed.insert(group_row(row, group, *old_value));
        if (new_value)
            table.added.insert(group_row(row, group, *new_value));
    }
}
}  // namespace ygg::database::datalog::detail
