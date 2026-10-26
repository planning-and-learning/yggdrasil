/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/datalog/detail/heuristic.hpp"

#include "yggdrasil/database/datalog/detail/executor.hpp"

#include <algorithm>
#include <functional>
#include <ranges>
#include <set>

namespace ygg::database::datalog::detail
{
namespace
{
bool reads_static_input(const CompiledProgram& program, const Literal& literal)
{
    if (literal.kind != Literal::Kind::POSITIVE)
        return false;
    const auto& table = *program.tables[literal.table];
    return table.slot.has_value() && table.is_static;
}

/// The variables of the terms.
template<typename Terms>
bool all_bound(const Terms& terms, const std::set<uint_t>& bound)
{
    return std::ranges::all_of(terms, [&](Term t) { return !t.is_variable() || bound.contains(t.value); });
}
}  // namespace

std::unique_ptr<GoalHeuristic> GoalHeuristic::make(CompiledProgram& program, const StratumInfo& stratum, std::span<const size_t> goals)
{
    if (goals.empty() || stratum.tables.size() != 1)
        return nullptr;
    const auto table = stratum.tables.front();
    const auto arity = program.tables[table]->arity;
    // The abstract rules read the key they expand from a table of their own.
    program.tables.push_back(std::make_unique<Table>(arity));
    program.recursive_tables.push_back(false);
    auto result = std::unique_ptr<GoalHeuristic>(new GoalHeuristic(program.tables.size() - 1, arity - 1));
    for (const auto r : stratum.rules)
    {
        const auto& rule = program.rules[r];
        auto sources = std::vector<size_t>();
        for (size_t j = 0; j < rule.literals.size(); ++j)
            if (rule.literals[j].kind == Literal::Kind::POSITIVE && rule.literals[j].table == table)
                sources.push_back(j);
        if (sources.empty())
            continue;  // A base rule; the targets' weights do not depend on it.
        // h is a sum of edge weights: max (bottleneck weights) would need another combination.
        if (sources.size() != 1
            || std::ranges::any_of(rule.literals,
                                   [](const Literal& l) { return l.kind == Literal::Kind::ARITHMETIC && l.operation != Arithmetic::Operation::ADD; }))
            return nullptr;
        // Keep the static literals and what they bind; dropping the others only adds abstract derivations.
        auto kept = std::vector<bool>(rule.literals.size());
        auto bound = std::set<uint_t>();
        const auto bind = [&](const Literal& l)
        {
            for (const auto t : l.terms)
                if (t.is_variable())
                    bound.insert(t.value);
        };
        kept[sources.front()] = true;
        bind(rule.literals[sources.front()]);
        for (bool grew = true; grew;)
        {
            grew = false;
            for (size_t i = 0; i < rule.literals.size(); ++i)
            {
                const auto& literal = rule.literals[i];
                if (kept[i])
                    continue;
                const bool keep = reads_static_input(program, literal)
                                  || (literal.kind == Literal::Kind::ARITHMETIC && all_bound(literal.terms | std::views::take(2), bound))
                                  || (literal.kind == Literal::Kind::COMPARISON && all_bound(literal.terms, bound));
                if (keep)
                {
                    kept[i] = grew = true;
                    bind(literal);
                }
            }
        }
        if (!all_bound(rule.head_terms, bound))
            return nullptr;
        auto edge = Edge { rule, 0 };
        edge.rule.literals.clear();
        for (size_t i = 0; i < rule.literals.size(); ++i)
            if (kept[i])
            {
                if (i == sources.front())
                {
                    edge.source = edge.rule.literals.size();
                    edge.rule.literals.push_back(rule.literals[i]);
                    edge.rule.literals.back().table = result->m_scratch;
                }
                else
                    edge.rule.literals.push_back(rule.literals[i]);
            }
        plan_rule(program, edge.rule);
        result->m_edges.push_back(std::move(edge));
    }
    if (result->m_edges.empty())
        return nullptr;
    for (const auto g : goals)
    {
        const auto& rule = program.rules[g];
        auto target = rule;
        target.literals.clear();
        auto key = std::vector<Term>();
        auto bound = std::set<uint_t>();
        for (const auto& literal : rule.literals)
        {
            if (literal.kind == Literal::Kind::POSITIVE && literal.table == table)
            {
                key.assign(literal.terms.begin(), literal.terms.end() - 1);
                continue;
            }
            target.literals.push_back(literal);
            if (literal.kind == Literal::Kind::POSITIVE || literal.kind == Literal::Kind::ARITHMETIC)
                for (const auto t : literal.terms)
                    if (t.is_variable())
                        bound.insert(t.value);
        }
        if (!all_bound(key, bound))
            return nullptr;
        target.head_terms = key;
        plan_rule(program, target);
        result->m_targets.push_back(std::move(target));
    }
    return result;
}

void GoalHeuristic::read_targets(CompiledProgram&, Executor& executor)
{
    m_target_keys.clear();
    for (const auto& rule : m_targets)
    {
        m_reads.assign(rule.literals.size(), Version::CURRENT);
        executor.run(rule, rule.plain, m_reads, [&](std::span<const uint_t> key) { m_target_keys.insert(key); });
    }
}

uint_t GoalHeuristic::operator()(CompiledProgram& program, Executor& executor, std::span<const uint_t> key)
{
    const auto index = m_keys.insert(key);
    if (index >= m_rows.size())
        m_rows.resize(m_keys.size());
    if (m_rows[index].empty())
        compute_row(program, executor, index);
    for (const auto& [weight, reached] : m_rows[index])
        if (m_target_keys.contains(m_keys[reached]))
            return weight;
    return Arithmetic::infinity;
}

void GoalHeuristic::compute_row(CompiledProgram& program, Executor& executor, uint_t index)
{
    auto& scratch = *program.tables[m_scratch];
    auto best = std::vector<uint_t>(m_keys.size(), Arithmetic::infinity);
    auto settled = std::vector<bool>(m_keys.size());
    auto queue = std::vector<std::pair<uint_t, uint_t>> { { 0, index } };
    best[index] = 0;
    auto row = std::vector<std::pair<uint_t, uint_t>>();
    auto expanded = std::vector<uint_t>();
    while (!queue.empty())
    {
        std::ranges::pop_heap(queue, std::greater {});
        const auto [weight, current] = queue.back();
        queue.pop_back();
        if (settled[current] || weight != best[current])
            continue;
        settled[current] = true;
        row.emplace_back(weight, current);
        // The abstract successors of the key, from the abstract rules reading the key at weight 0.
        expanded.assign(m_keys[current].begin(), m_keys[current].end());
        expanded.push_back(0);
        scratch.frontier.clear();
        scratch.frontier.insert(expanded);
        m_derived.clear();
        for (const auto& edge : m_edges)
        {
            m_reads.assign(edge.rule.literals.size(), Version::CURRENT);
            m_reads[edge.source] = Version::FRONTIER;
            executor.run(edge.rule,
                         edge.rule.by_source[edge.source],
                         m_reads,
                         [&](std::span<const uint_t> derived) { m_derived.emplace_back(derived.begin(), derived.end()); });
        }
        scratch.frontier.clear();
        for (const auto& derived : m_derived)
        {
            const auto successor = m_keys.insert(std::span<const uint_t>(derived).first(derived.size() - 1));
            if (successor >= best.size())
            {
                best.resize(m_keys.size(), Arithmetic::infinity);
                settled.resize(m_keys.size());
            }
            const auto candidate = Arithmetic { Arithmetic::Operation::ADD, {}, {}, 0 }.apply(weight, derived.back());
            if (candidate < best[successor])
            {
                best[successor] = candidate;
                queue.emplace_back(candidate, successor);
                std::ranges::push_heap(queue, std::greater {});
            }
        }
    }
    if (index >= m_rows.size())
        m_rows.resize(m_keys.size());
    m_rows[index] = std::move(row);
}

size_t GoalHeuristic::memory_usage() const noexcept
{
    using Row = std::vector<std::pair<uint_t, uint_t>>;
    size_t result = m_target_keys.memory_usage() + m_keys.memory_usage() + m_rows.capacity() * sizeof(Row);
    for (const auto& row : m_rows)
        result += row.capacity() * sizeof(Row::value_type);
    return result;
}
}  // namespace ygg::database::datalog::detail
