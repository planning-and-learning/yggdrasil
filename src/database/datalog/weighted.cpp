/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/datalog/detail/executor.hpp"
#include "yggdrasil/database/datalog/detail/heuristic.hpp"
#include "yggdrasil/database/datalog/detail/strata.hpp"

#include <algorithm>
#include <functional>

namespace ygg::database::datalog::detail
{
WeightedStratum::WeightedStratum(StratumInfo stratum, const CompiledProgram& program, std::vector<size_t> goals, std::unique_ptr<GoalHeuristic> heuristic) :
    Stratum(std::move(stratum)),
    m_goals(std::move(goals)),
    m_holds(m_goals.size()),
    m_heuristic(std::move(heuristic))
{
    for (const auto table : info().tables)
    {
        const auto arity = program.tables[table]->arity;
        m_settled.emplace_back(arity - 1);
        m_queued.emplace_back(arity - 1);
        m_best.emplace_back();
        m_snapshot.emplace_back(arity);
    }
}

WeightedStratum::~WeightedStratum() = default;

void WeightedStratum::offer(CompiledProgram& program, Executor& executor, size_t position, std::span<const uint_t> row)
{
    const auto key = row.first(row.size() - 1);
    const auto weight = row.back();
    if (m_settled[position].contains(key))
        return;
    const auto estimate = m_heuristic ? (*m_heuristic)(program, executor, key) : 0;
    if (estimate == Arithmetic::infinity)
        return;
    auto& queued = m_queued[position];
    auto& best = m_best[position];
    const auto size = queued.size();
    const auto index = queued.insert(key);
    if (index == size)
        best.push_back(weight);
    else if (weight >= best[index])
        return;
    else
        best[index] = weight;
    m_queue.push_back(
        Candidate { Arithmetic { Arithmetic::Operation::ADD, {}, {}, 0 }.apply(weight, estimate), position, std::vector<uint_t>(row.begin(), row.end()) });
    std::ranges::push_heap(m_queue, std::greater {});
}

void WeightedStratum::offer_derived(CompiledProgram& program, Executor& executor)
{
    for (const auto& [position, row] : m_derived)
        offer(program, executor, position, row);
    m_derived.clear();
}

bool WeightedStratum::goals_hold(CompiledProgram& program, Executor& executor, size_t table)
{
    for (size_t g = 0; g < m_goals.size(); ++g)
    {
        if (m_holds[g])
            continue;
        const auto& rule = program.rules[m_goals[g]];
        for (size_t j = 0; j < rule.literals.size() && !m_holds[g]; ++j)
        {
            if (rule.literals[j].table != table)
                continue;
            auto& read = reads(rule, Version::CURRENT);
            read[j] = Version::FRONTIER;
            executor.run(rule, rule.by_source[j], read, [&](std::span<const uint_t>) { m_holds[g] = true; });
        }
    }
    return std::ranges::all_of(m_holds, [](bool holds) { return holds; });
}

void WeightedStratum::derive(CompiledProgram& program, Executor& executor)
{
    m_holds.assign(m_goals.size(), false);
    for (size_t i = 0; i < info().tables.size(); ++i)
    {
        program.tables[info().tables[i]]->clear();
        m_settled[i].clear();
        m_queued[i].clear();
        m_best[i].clear();
    }
    m_queue.clear();
    if (m_heuristic)
        m_heuristic->read_targets(program, executor);
    // Rules that read no relation of the stratum start the queue.
    for (const auto r : info().rules)
    {
        const auto& rule = program.rules[r];
        if (std::ranges::any_of(rule.literals, [&](const Literal& l) { return l.kind == Literal::Kind::POSITIVE && info().contains(l.table); }))
            continue;
        const auto position = info().position(rule.head);
        executor.run(rule,
                     rule.plain,
                     reads(rule, Version::CURRENT),
                     [&](std::span<const uint_t> row) { m_derived.emplace_back(position, std::vector<uint_t>(row.begin(), row.end())); });
        offer_derived(program, executor);
    }
    // Settle rows in order of weight; a settled row is expanded with the rows settled before it.
    while (!m_queue.empty())
    {
        std::ranges::pop_heap(m_queue, std::greater {});
        auto candidate = std::move(m_queue.back());
        m_queue.pop_back();
        const auto row = std::span<const uint_t>(candidate.row);
        const auto key = row.first(row.size() - 1);
        auto& settled = m_settled[candidate.position];
        // A lighter derivation of the row was queued after this one, or settled. A queued row has a best weight.
        const auto queued = m_queued[candidate.position].find(key);
        if (settled.contains(key) || !queued || row.back() != m_best[candidate.position][*queued])
            continue;
        settled.insert(key);
        const auto table_index = info().tables[candidate.position];
        auto& table = *program.tables[table_index];
        table.current.insert(row);
        table.frontier.clear();
        table.frontier.insert(row);
        for (const auto r : info().rules)
        {
            const auto& rule = program.rules[r];
            const auto position = info().position(rule.head);
            for (size_t j = 0; j < rule.literals.size(); ++j)
            {
                if (rule.literals[j].kind != Literal::Kind::POSITIVE || rule.literals[j].table != table_index)
                    continue;
                auto& read = reads(rule, Version::CURRENT);
                read[j] = Version::FRONTIER;
                executor.run(rule,
                             rule.by_source[j],
                             read,
                             [&](std::span<const uint_t> derived) { m_derived.emplace_back(position, std::vector<uint_t>(derived.begin(), derived.end())); });
            }
        }
        offer_derived(program, executor);
        const bool done = !m_goals.empty() && goals_hold(program, executor, table_index);
        table.frontier.clear();
        if (done)
            break;
    }
    m_queue.clear();
}

void WeightedStratum::evaluate(CompiledProgram& program, Executor& executor) { derive(program, executor); }

void WeightedStratum::update(CompiledProgram& program, Executor& executor)
{
    for (size_t i = 0; i < info().tables.size(); ++i)
    {
        auto& snapshot = m_snapshot[i];
        snapshot.clear();
        program.tables[info().tables[i]]->current.for_each([&](std::span<const uint_t> row) { snapshot.insert(row); });
    }
    derive(program, executor);
    for (size_t i = 0; i < info().tables.size(); ++i)
    {
        auto& table = *program.tables[info().tables[i]];
        auto& snapshot = m_snapshot[i];
        table.current.for_each(
            [&](std::span<const uint_t> row)
            {
                if (!snapshot.contains(row))
                    table.added.insert(row);
            });
        for (uint_t r = 0; r < snapshot.size(); ++r)
            if (!table.current.contains(snapshot[r]))
                table.removed.insert(snapshot[r]);
        snapshot.clear();
    }
}

size_t WeightedStratum::state_memory_usage() const noexcept
{
    size_t result = m_queue.capacity() * sizeof(Candidate) + (m_heuristic ? m_heuristic->memory_usage() : 0);
    for (const auto& candidate : m_queue)
        result += candidate.row.capacity() * sizeof(uint_t);
    for (size_t i = 0; i < m_settled.size(); ++i)
        result += m_settled[i].memory_usage() + m_queued[i].memory_usage() + m_best[i].capacity() * sizeof(uint_t) + m_snapshot[i].memory_usage();
    return result;
}
}  // namespace ygg::database::datalog::detail
