/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_CONJUNCTIVE_PLAN_HPP_
#define YGG_DATABASE_SEMANTICS_CONJUNCTIVE_PLAN_HPP_

#include "yggdrasil/database/optimization/statistics.hpp"
#include "yggdrasil/database/syntax/translations/union_normal_form.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace ygg::database
{

/// One atom of a conjunctive plan: the columns bound on arrival (its lookup key), the variables it binds, and the
/// filters that become checkable after it.
struct ConjunctiveStep
{
    static constexpr size_t unresolved = std::numeric_limits<size_t>::max();

    size_t atom = 0;
    std::vector<size_t> positions;                     ///< The atom's columns bound on arrival, in column order.
    std::vector<std::pair<size_t, uint32_t>> assign;   ///< Column, variable bound here.
    std::vector<std::pair<size_t, uint32_t>> compare;  ///< Column, variable bound earlier in this row.
    std::vector<size_t> equal;
    std::vector<size_t> value;
    std::vector<size_t> negations;
    size_t index = unresolved;  ///< The executor's lookup structure for (slot, positions); set by the executor.
};

/// A branch's atoms in evaluation order for one set of initially bound variables. Filters whose variables are bound
/// from the start are checked before the first step; negations carry plans of their own, their outer variables bound.
struct ConjunctivePlan
{
    std::vector<size_t> equal;
    std::vector<size_t> value;
    std::vector<size_t> negations;
    std::vector<ConjunctiveStep> steps;
    std::vector<std::vector<ConjunctivePlan>> negation_plans;  ///< Per negation, per branch.
};

/// A greedy left-deep order for index nested-loop evaluation: next the atom with the smallest expected fanout, using
/// System R's uniform estimate that divides an input's rows by the distinct values of each bound column (Selinger et
/// al., "Access Path Selection in a Relational Database Management System", SIGMOD 1979). Each equality, constant and
/// negation attaches to the first step after which its variables are bound. `statistics(slot)` measures an input.
template<typename Statistics>
    requires std::invocable<Statistics&, size_t>
ConjunctivePlan plan_branch(const UnionNormalForm& query, const ConjunctiveBranch& branch, std::vector<bool> bound, Statistics&& statistics)
{
    const auto fanout = [&](const ConjunctiveAtom& atom)
    {
        const RelationStatistics& measured = statistics(atom.slot);
        auto estimate = measured.rows;
        for (size_t c = 0; c < atom.vars.size(); ++c)
            if (bound[atom.vars[c]] && estimate > 0)
                estimate /= measured.distinct.at(query.input_columns[atom.slot][c].label);
        return estimate;
    };
    auto plan = ConjunctivePlan {};
    auto pending_equal = std::vector<bool>(branch.equal.size(), true);
    auto pending_value = std::vector<bool>(branch.value.size(), true);
    auto pending_negation = std::vector<bool>(branch.negations.size(), true);
    const auto attach = [&](std::vector<size_t>& equal, std::vector<size_t>& value, std::vector<size_t>& negations)
    {
        for (size_t i = 0; i < branch.equal.size(); ++i)
            if (pending_equal[i] && bound[branch.equal[i].first] && bound[branch.equal[i].second])
            {
                pending_equal[i] = false;
                equal.push_back(i);
            }
        for (size_t i = 0; i < branch.value.size(); ++i)
            if (pending_value[i] && bound[branch.value[i].first])
            {
                pending_value[i] = false;
                value.push_back(i);
            }
        for (size_t i = 0; i < branch.negations.size(); ++i)
            if (pending_negation[i] && std::ranges::all_of(branch.negations[i].outer, [&](uint32_t var) { return bool(bound[var]); }))
            {
                pending_negation[i] = false;
                negations.push_back(i);
            }
    };
    attach(plan.equal, plan.value, plan.negations);
    auto remaining = std::vector<size_t>(branch.atoms.size());
    for (size_t i = 0; i < remaining.size(); ++i)
        remaining[i] = i;
    while (!remaining.empty())
    {
        const auto best = std::ranges::min_element(remaining, {}, [&](size_t atom) { return fanout(branch.atoms[atom]); });
        const auto& atom = branch.atoms[*best];
        auto& step = plan.steps.emplace_back();
        step.atom = *best;
        auto assigned = std::vector<uint32_t>();
        for (size_t c = 0; c < atom.vars.size(); ++c)
        {
            const auto var = atom.vars[c];
            if (bound[var])
                step.positions.push_back(c);
            else if (std::ranges::find(assigned, var) != assigned.end())
                step.compare.emplace_back(c, var);
            else
            {
                step.assign.emplace_back(c, var);
                assigned.push_back(var);
            }
        }
        for (const auto var : assigned)
            bound[var] = true;
        remaining.erase(best);
        attach(step.equal, step.value, step.negations);
    }
    for (const auto& negation : branch.negations)
    {
        auto outer = std::vector<bool>(query.variables.size(), false);
        for (const auto var : negation.outer)
            outer[var] = true;
        auto& plans = plan.negation_plans.emplace_back();
        for (const auto& inner : negation.branches)
            plans.push_back(plan_branch(query, inner, outer, statistics));
    }
    return plan;
}

}  // namespace ygg::database

#endif
