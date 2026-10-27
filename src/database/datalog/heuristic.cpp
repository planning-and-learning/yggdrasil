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
    // The reversed rules read the head row they expand from a table of their own.
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
        // Keep the static literals and what they compute; dropping the others only adds abstract derivations. The read
        // row's weight is taken as 0, so that the head weight is the edge's.
        const auto& source = rule.literals[sources.front()];
        const auto zero = [&](Term t) { return t == source.terms.back() ? Term::constant(0) : t; };
        auto edge = rule;
        edge.literals.clear();
        auto head = Literal { Literal::Kind::POSITIVE, result->m_scratch, rule.head_terms };
        head.terms.back() = Term::variable(static_cast<uint_t>(edge.slots++));
        edge.literals.push_back(head);
        for (size_t i = 0; i < rule.literals.size(); ++i)
        {
            auto literal = rule.literals[i];
            if (i == sources.front()
                || !(reads_static_input(program, literal) || literal.kind == Literal::Kind::ARITHMETIC || literal.kind == Literal::Kind::COMPARISON))
                continue;
            if (literal.kind == Literal::Kind::ARITHMETIC)
                for (size_t t = 0; t < 2; ++t)
                    literal.terms[t] = zero(literal.terms[t]);
            edge.literals.push_back(literal);
        }
        edge.head_terms = source.terms;
        edge.head_terms.back() = zero(rule.head_terms.back());
        // From the head row, the static literals and arithmetic must bind the read row and the edge's weight; arithmetic
        // and comparisons over what they leave unbound are dropped.
        auto bound = std::set<uint_t>();
        const auto bind = [&](const auto& terms)
        {
            for (const auto t : terms)
                if (t.is_variable())
                    bound.insert(t.value);
        };
        bind(head.terms);
        for (bool grew = true; grew;)
        {
            grew = false;
            for (const auto& literal : edge.literals)
            {
                const auto before = bound.size();
                if (literal.kind == Literal::Kind::POSITIVE)
                    bind(literal.terms);
                else if (literal.kind == Literal::Kind::ARITHMETIC && all_bound(literal.terms | std::views::take(2), bound))
                    bind(literal.terms | std::views::drop(2));
                grew = grew || bound.size() != before;
            }
        }
        std::erase_if(edge.literals,
                      [&](const Literal& l)
                      { return (l.kind == Literal::Kind::ARITHMETIC || l.kind == Literal::Kind::COMPARISON) && !all_bound(l.terms, bound); });
        if (!all_bound(edge.head_terms, bound))
            return nullptr;
        plan_rule(program, edge);
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

void GoalHeuristic::read_targets(CompiledProgram& program, Executor& executor)
{
    m_target_keys.clear();
    for (const auto& rule : m_targets)
    {
        m_reads.assign(rule.literals.size(), Version::CURRENT);
        executor.run(rule, rule.plain, m_reads, [&](std::span<const uint_t> key) { m_target_keys.insert(key); });
    }
    auto rows = std::vector<std::span<const uint_t>>();
    for (uint_t i = 0; i < m_target_keys.size(); ++i)
        rows.push_back(m_target_keys[i]);
    std::ranges::sort(rows, [](auto lhs, auto rhs) { return std::ranges::lexicographical_compare(lhs, rhs); });
    auto targets = std::vector<uint_t>();
    for (const auto row : rows)
        targets.insert(targets.end(), row.begin(), row.end());
    auto it = m_weights.find(targets);
    if (it == m_weights.end())
    {
        auto weights = backward(program, executor);
        it = m_weights.emplace(std::move(targets), std::move(weights)).first;
    }
    m_current = &it->second;
}

uint_t GoalHeuristic::operator()(std::span<const uint_t> key) const
{
    const auto index = m_keys.find(key);
    return index && *index < m_current->size() ? (*m_current)[*index] : Arithmetic::infinity;
}

std::vector<uint_t> GoalHeuristic::backward(CompiledProgram& program, Executor& executor)
{
    auto& scratch = *program.tables[m_scratch];
    auto best = std::vector<uint_t>();
    auto queue = std::vector<std::pair<uint_t, uint_t>>();
    const auto relax = [&](std::span<const uint_t> key, uint_t weight)
    {
        const auto index = m_keys.insert(key);
        if (index >= best.size())
            best.resize(m_keys.size(), Arithmetic::infinity);
        if (weight < best[index])
        {
            best[index] = weight;
            queue.emplace_back(weight, index);
            std::ranges::push_heap(queue, std::greater {});
        }
    };
    for (uint_t i = 0; i < m_target_keys.size(); ++i)
        relax(m_target_keys[i], 0);
    auto expanded = std::vector<uint_t>();
    while (!queue.empty())
    {
        std::ranges::pop_heap(queue, std::greater {});
        const auto [weight, current] = queue.back();
        queue.pop_back();
        if (weight != best[current])
            continue;
        // The abstract predecessors of the key, with the weights of their edges to it.
        expanded.assign(m_keys[current].begin(), m_keys[current].end());
        expanded.push_back(0);
        scratch.frontier.clear();
        scratch.frontier.insert(expanded);
        m_derived.clear();
        for (const auto& edge : m_edges)
        {
            m_reads.assign(edge.literals.size(), Version::CURRENT);
            m_reads[0] = Version::FRONTIER;
            executor.run(edge, edge.by_source[0], m_reads, [&](std::span<const uint_t> derived) { m_derived.emplace_back(derived.begin(), derived.end()); });
        }
        scratch.frontier.clear();
        for (const auto& derived : m_derived)
            relax(std::span<const uint_t>(derived).first(derived.size() - 1),
                  Arithmetic { Arithmetic::Operation::ADD, {}, {}, 0 }.apply(weight, derived.back()));
    }
    return best;
}

size_t GoalHeuristic::memory_usage() const noexcept
{
    size_t result = m_target_keys.memory_usage() + m_keys.memory_usage();
    for (const auto& [targets, weights] : m_weights)
        result += (targets.capacity() + weights.capacity()) * sizeof(uint_t);
    return result;
}
}  // namespace ygg::database::datalog::detail
