/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/datalog/detail/heuristic.hpp"

#include "yggdrasil/database/datalog/detail/executor.hpp"

#include <algorithm>
#include <functional>
#include <numeric>
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
    const auto key_arity = program.tables[table]->arity - 1;
    auto rules = std::vector<size_t>();
    for (const auto r : stratum.rules)
    {
        const auto& rule = program.rules[r];
        const auto sources = std::ranges::count_if(rule.literals, [&](const Literal& l) { return l.kind == Literal::Kind::POSITIVE && l.table == table; });
        if (sources == 0)
            continue;  // A base rule; the targets' weights do not depend on it.
        // h is a sum of edge weights: max (bottleneck weights) would need another combination.
        if (sources != 1
            || std::ranges::any_of(rule.literals,
                                   [](const Literal& l) { return l.kind == Literal::Kind::ARITHMETIC && l.operation != Arithmetic::Operation::ADD; }))
            return nullptr;
        rules.push_back(r);
    }
    if (rules.empty())
        return nullptr;
    // The key columns that every goal's other literals bind: the targets' columns. A column a goal leaves unbound
    // (any value is a target) is kept by no projection.
    auto keys = std::vector<std::vector<Term>>();
    auto target_columns = std::vector<size_t>();
    for (size_t c = 0; c < key_arity; ++c)
        target_columns.push_back(c);
    for (const auto g : goals)
    {
        const auto& rule = program.rules[g];
        auto bound = std::set<uint_t>();
        for (const auto& literal : rule.literals)
        {
            if (literal.kind == Literal::Kind::POSITIVE && literal.table == table)
                keys.emplace_back(literal.terms.begin(), literal.terms.end() - 1);
            else if (literal.kind == Literal::Kind::POSITIVE || literal.kind == Literal::Kind::ARITHMETIC)
                for (const auto t : literal.terms)
                    if (t.is_variable())
                        bound.insert(t.value);
        }
        std::erase_if(target_columns, [&](size_t c) { return keys.back()[c].is_variable() && !bound.contains(keys.back()[c].value); });
    }
    if (target_columns.empty())
        return nullptr;
    auto result = std::unique_ptr<GoalHeuristic>(new GoalHeuristic(target_columns.size()));
    result->m_target_columns = target_columns;
    for (size_t i = 0; i < goals.size(); ++i)
    {
        const auto& rule = program.rules[goals[i]];
        auto target = rule;
        target.literals.clear();
        for (const auto& literal : rule.literals)
            if (!(literal.kind == Literal::Kind::POSITIVE && literal.table == table))
                target.literals.push_back(literal);
        target.head_terms.clear();
        for (const auto c : target_columns)
            target.head_terms.push_back(keys[i][c]);
        plan_rule(program, target);
        result->m_targets.push_back(std::move(target));
    }
    // The projections: every nonempty set of key columns, or for wide keys the single columns and the whole key.
    auto subsets = std::vector<std::vector<size_t>>();
    if (key_arity <= 4)
        for (size_t mask = 1; mask < (size_t { 1 } << key_arity); ++mask)
        {
            auto& columns = subsets.emplace_back();
            for (size_t c = 0; c < key_arity; ++c)
                if (mask & (size_t { 1 } << c))
                    columns.push_back(c);
        }
    else
    {
        for (size_t c = 0; c < key_arity; ++c)
            subsets.push_back({ c });
        auto& all = subsets.emplace_back(key_arity);
        std::iota(all.begin(), all.end(), size_t { 0 });
    }
    std::erase_if(subsets, [&](const std::vector<size_t>& columns) { return !std::ranges::includes(target_columns, columns); });
    for (auto& columns : subsets)
    {
        const auto project = [&](const std::vector<Term>& terms)
        {
            auto projected = std::vector<Term>();
            for (const auto c : columns)
                projected.push_back(terms[c]);
            return projected;
        };
        auto projection = Projection { columns, program.tables.size(), {}, RawArraySet<uint_t>(columns.size()), {}, nullptr, true };
        auto edges = std::vector<CompiledRule>();
        for (const auto r : rules)
        {
            const auto& rule = program.rules[r];
            const auto& source = *std::ranges::find_if(rule.literals, [&](const Literal& l) { return l.kind == Literal::Kind::POSITIVE && l.table == table; });
            // The read row's weight is taken as 0, so that the head weight is the edge's.
            const auto zero = [&](Term t) { return t == source.terms.back() ? Term::constant(0) : t; };
            auto edge = rule;
            edge.literals.clear();
            auto head = Literal { Literal::Kind::POSITIVE, projection.scratch, project(rule.head_terms) };
            head.terms.push_back(Term::variable(static_cast<uint_t>(edge.slots++)));
            edge.literals.push_back(head);
            for (const auto& original : rule.literals)
            {
                auto literal = original;
                if (&original == &source
                    || !(reads_static_input(program, literal) || literal.kind == Literal::Kind::ARITHMETIC || literal.kind == Literal::Kind::COMPARISON))
                    continue;
                if (literal.kind == Literal::Kind::ARITHMETIC)
                    for (size_t t = 0; t < 2; ++t)
                        literal.terms[t] = zero(literal.terms[t]);
                edge.literals.push_back(literal);
            }
            edge.head_terms = project(source.terms);
            edge.head_terms.push_back(zero(rule.head_terms.back()));
            // From the projected head row, the static literals and arithmetic must bind the projected read row and the
            // edge's weight; arithmetic and comparisons over what they leave unbound are dropped.
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
                const auto before = bound.size();
                for (const auto& literal : edge.literals)
                    if (literal.kind == Literal::Kind::POSITIVE)
                        bind(literal.terms);
                    else if (literal.kind == Literal::Kind::ARITHMETIC && all_bound(literal.terms | std::views::take(2), bound))
                        bind(literal.terms | std::views::drop(2));
                grew = bound.size() != before;
            }
            std::erase_if(edge.literals,
                          [&](const Literal& l)
                          { return (l.kind == Literal::Kind::ARITHMETIC || l.kind == Literal::Kind::COMPARISON) && !all_bound(l.terms, bound); });
            if (!all_bound(edge.head_terms, bound))
            {
                edges.clear();
                break;
            }
            edges.push_back(std::move(edge));
        }
        if (edges.empty())
            continue;
        program.tables.push_back(std::make_unique<Table>(columns.size() + 1));
        program.recursive_tables.push_back(false);
        for (auto& edge : edges)
            plan_rule(program, edge);
        projection.edges = std::move(edges);
        result->m_projections.push_back(std::move(projection));
    }
    if (result->m_projections.empty())
        return nullptr;
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
    for (auto& projection : m_projections)
    {
        if (!projection.usable)
            continue;
        auto targets = RawArraySet<uint_t>(projection.columns.size());
        for (uint_t i = 0; i < m_target_keys.size(); ++i)
        {
            m_key.clear();
            for (const auto c : projection.columns)
                m_key.push_back(m_target_keys[i][static_cast<size_t>(std::ranges::find(m_target_columns, c) - m_target_columns.begin())]);
            targets.insert(m_key);
        }
        auto rows = std::vector<std::span<const uint_t>>();
        for (uint_t i = 0; i < targets.size(); ++i)
            rows.push_back(targets[i]);
        std::ranges::sort(rows, [](auto lhs, auto rhs) { return std::ranges::lexicographical_compare(lhs, rhs); });
        auto flat = std::vector<uint_t>();
        for (const auto row : rows)
            flat.insert(flat.end(), row.begin(), row.end());
        auto it = projection.weights.find(flat);
        if (it == projection.weights.end())
        {
            auto weights = backward(program, executor, projection, targets);
            if (!weights)
            {
                projection = Projection { {}, projection.scratch, {}, RawArraySet<uint_t>(1), {}, nullptr, false };
                continue;
            }
            it = projection.weights.emplace(std::move(flat), std::move(*weights)).first;
        }
        projection.current = &it->second;
    }
}

uint_t GoalHeuristic::operator()(std::span<const uint_t> key)
{
    auto result = uint_t { 0 };
    for (const auto& projection : m_projections)
    {
        if (!projection.usable)
            continue;
        m_key.clear();
        for (const auto c : projection.columns)
            m_key.push_back(key[c]);
        const auto index = projection.keys.find(m_key);
        if (!index || *index >= projection.current->size())
            return Arithmetic::infinity;
        result = std::max(result, (*projection.current)[*index]);
    }
    return result;
}

std::optional<std::vector<uint_t>>
GoalHeuristic::backward(CompiledProgram& program, Executor& executor, Projection& projection, const RawArraySet<uint_t>& targets)
{
    // Enough for the projections a search can use; a larger abstract space than this is dropped.
    constexpr size_t budget = size_t { 1 } << 18;
    auto& scratch = *program.tables[projection.scratch];
    auto best = std::vector<uint_t>();
    auto queue = std::vector<std::pair<uint_t, uint_t>>();
    const auto relax = [&](std::span<const uint_t> key, uint_t weight)
    {
        const auto index = projection.keys.insert(key);
        if (index >= best.size())
            best.resize(projection.keys.size(), Arithmetic::infinity);
        if (weight < best[index])
        {
            best[index] = weight;
            queue.emplace_back(weight, index);
            std::ranges::push_heap(queue, std::greater {});
        }
    };
    for (uint_t i = 0; i < targets.size(); ++i)
        relax(targets[i], 0);
    auto expanded = std::vector<uint_t>();
    for (size_t expansions = 0; !queue.empty(); ++expansions)
    {
        if (expansions == budget)
            return std::nullopt;
        std::ranges::pop_heap(queue, std::greater {});
        const auto [weight, current] = queue.back();
        queue.pop_back();
        if (weight != best[current])
            continue;
        // The abstract predecessors of the key, with the weights of their edges to it.
        expanded.assign(projection.keys[current].begin(), projection.keys[current].end());
        expanded.push_back(0);
        scratch.frontier.clear();
        scratch.frontier.insert(expanded);
        m_derived.clear();
        for (const auto& edge : projection.edges)
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
    size_t result = m_target_keys.memory_usage();
    for (const auto& projection : m_projections)
    {
        result += projection.keys.memory_usage();
        for (const auto& [targets, weights] : projection.weights)
            result += (targets.capacity() + weights.capacity()) * sizeof(uint_t);
    }
    return result;
}
}  // namespace ygg::database::datalog::detail
