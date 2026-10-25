/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/datalog/details/program.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

/// Plans each rule once per literal as the first generator, once with none and once with the head bound: filters as
/// soon as their terms are bound, then the positive literal with the most bound columns, intersected with the literals
/// it completes; indexes are created per access pattern.
namespace ygg::database::datalog::detail
{
namespace
{
Step generator(CompiledProgram& program, const CompiledRule& rule, size_t j, std::vector<bool>& bound)
{
    const auto& literal = rule.literals[j];
    auto step = Step { j, true };
    auto columns = std::vector<size_t>();
    auto bound_here = std::vector<uint_t>();
    for (size_t c = 0; c < literal.terms.size(); ++c)
    {
        const auto t = literal.terms[c];
        if (!t.is_variable() || bound[t.value])
        {
            columns.push_back(c);
            step.key.push_back(t);
        }
        else if (std::ranges::find(bound_here, t.value) != bound_here.end())
            step.checks.emplace_back(c, t.value);
        else
        {
            step.binds.emplace_back(c, t.value);
            bound_here.push_back(t.value);
        }
    }
    for (const auto slot : bound_here)
        bound[slot] = true;
    if (!columns.empty())
        step.index = program.tables[literal.table]->add_index(columns);
    return step;
}

Plan plan(CompiledProgram& program, const CompiledRule& rule, std::optional<size_t> source, std::vector<bool> bound)
{
    auto result = Plan {};
    auto placed = std::vector<bool>(rule.literals.size());
    const auto all_bound = [&](const std::vector<Term>& terms, size_t count)
    { return std::all_of(terms.begin(), terms.begin() + static_cast<std::ptrdiff_t>(count), [&](Term t) { return !t.is_variable() || bound[t.value]; }); };
    if (source)
    {
        result.steps.push_back(generator(program, rule, *source, bound));
        placed[*source] = true;
    }
    while (std::ranges::find(placed, false) != placed.end())
    {
        bool progressed = false;
        for (size_t j = 0; j < rule.literals.size(); ++j)
        {
            const auto& literal = rule.literals[j];
            if (placed[j])
                continue;
            if ((literal.kind == Literal::Kind::NEGATED || literal.kind == Literal::Kind::COMPARISON) && all_bound(literal.terms, literal.terms.size()))
            {
                result.steps.emplace_back(j, false);
                placed[j] = progressed = true;
            }
            else if (literal.kind == Literal::Kind::AGGREGATE && all_bound(literal.terms, literal.terms.size() - 1))
            {
                // Look up the group; the result is bound from its row.
                auto step = Step { j, false };
                const auto group = literal.terms.size() - 1;
                auto columns = std::vector<size_t>();
                for (size_t c = 0; c < group; ++c)
                {
                    columns.push_back(c);
                    step.key.push_back(literal.terms[c]);
                }
                if (!columns.empty())
                    step.index = program.tables[literal.table]->add_index(columns);
                if (bound[literal.terms.back().value])
                    step.checks.emplace_back(group, literal.terms.back().value);
                else
                    step.binds.emplace_back(group, literal.terms.back().value);
                bound[literal.terms.back().value] = true;
                result.steps.push_back(std::move(step));
                placed[j] = progressed = true;
            }
        }
        if (progressed)
            continue;
        // The positive literal with the most bound columns; on a tie one outside the head's stratum, whose relations
        // are typically the smaller ones (inputs, lower strata).
        auto best = npos;
        auto best_rank = std::pair<size_t, bool> {};
        for (size_t j = 0; j < rule.literals.size(); ++j)
        {
            const auto& literal = rule.literals[j];
            if (placed[j] || literal.kind != Literal::Kind::POSITIVE)
                continue;
            const auto count = static_cast<size_t>(std::ranges::count_if(literal.terms, [&](Term t) { return !t.is_variable() || bound[t.value]; }));
            const auto rank = std::pair { count, !program.recursive_tables[literal.table] };
            if (best == npos || rank > best_rank)
            {
                best = j;
                best_rank = rank;
            }
        }
        if (best == npos)
            throw std::logic_error("Datalog evaluator: a rule has a literal whose variables no positive atom binds.");
        const auto before = bound;
        const auto unbound = [&](const Literal& literal)
        {
            auto variables = std::set<uint_t>();
            for (const auto t : literal.terms)
                if (t.is_variable() && !before[t.value])
                    variables.insert(t.value);
            return variables;
        };
        const auto binds = unbound(rule.literals[best]);
        auto step = generator(program, rule, best, bound);
        placed[best] = true;
        for (size_t j = 0; j < rule.literals.size(); ++j)
            if (!placed[j] && rule.literals[j].kind == Literal::Kind::POSITIVE && !binds.empty() && unbound(rule.literals[j]) == binds)
            {
                auto peer_bound = before;
                step.peers.push_back(generator(program, rule, j, peer_bound));
                placed[j] = true;
            }
        result.steps.push_back(std::move(step));
    }
    return result;
}
}  // namespace

void plan_rules(CompiledProgram& program)
{
    for (auto& rule : program.rules)
    {
        const auto none = std::vector<bool>(rule.slots);
        rule.plain = plan(program, rule, std::nullopt, none);
        for (size_t j = 0; j < rule.literals.size(); ++j)
            rule.by_source.push_back(is_generator_kind(rule.literals[j].kind) ? plan(program, rule, j, none) : Plan {});
        if (program.recursive_tables[rule.head])
        {
            auto bound = none;
            for (const auto t : rule.head_terms)
                bound[t.value] = true;
            rule.rederive = plan(program, rule, std::nullopt, bound);
        }
    }
}
}  // namespace ygg::database::datalog::detail
