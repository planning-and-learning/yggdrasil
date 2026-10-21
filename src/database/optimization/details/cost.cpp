/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/optimization/details/cost.hpp"

namespace ygg::database::detail
{
const CostBasedPlanner::Refined& CostBasedPlanner::refine(size_t node)
{
    if (m_refined[node])
        return *m_refined[node];
    Refined result;
    const auto [subset, lhs, rhs] = m_tree[node];
    if (lhs)
    {
        const auto& left = refine(*lhs);
        const auto& right = refine(*rhs);
        const auto largest_input = std::max(rows(m_tree[*lhs].subset), rows(m_tree[*rhs].subset));
        const bool growing = rows(subset) - largest_input > FloatTolerance<float_t>::tolerance(rows(subset), largest_input);
        if (growing || left.multiway || right.multiway)
        {
            std::vector<size_t> inputs;
            for (const auto& [child, refined] : { std::pair(*lhs, &left), std::pair(*rhs, &right) })
                if (refined->multiway)
                    inputs.insert(inputs.end(), refined->inputs.begin(), refined->inputs.end());
                else
                    inputs.push_back(child);
            result = { true, std::move(inputs) };
        }
    }
    m_refined[node] = std::move(result);
    return *m_refined[node];
}
}

namespace ygg::database::detail
{
QueryView<> CostBasedPlanner::build(size_t node)
{
    const auto [subset, lhs, rhs] = m_tree[node];
    const auto keep = needed(subset);
    if (!lhs)
        return m_build->restrict(m_block->atoms[std::countr_zero(subset)], keep);
    // Multi-way joins with only two inputs are built as binary joins (Freitag et al.).
    const auto& refined = refine(node);
    if (!refined.multiway || refined.inputs.size() == 2)
        return m_build->restrict(m_build->join(build(*lhs), build(*rhs)), keep);
    std::vector<QueryView<>> inputs;
    for (const auto input : refined.inputs)
        inputs.push_back(build(input));
    return m_build->restrict(m_build->generic_join(inputs, variable_order(inputs)), keep);
}
}

namespace ygg::database::detail
{
QueryView<> CostBasedPlanner::plan() &&
{
    const auto count = m_block->atoms.size();
    std::vector<std::uint64_t> graph(count);
    for (size_t lhs = 0; lhs < count; ++lhs)
        for (size_t rhs = lhs + 1; rhs < count; ++rhs)
            if (itertools::intersects(m_variables[lhs], m_variables[rhs]))
            {
                graph[lhs] |= std::uint64_t { 1 } << rhs;
                graph[rhs] |= std::uint64_t { 1 } << lhs;
            }
    enumerate_connected_pairs(graph,
                                      [&](std::uint64_t lhs, std::uint64_t rhs)
                                      {
                                          const auto subset = lhs | rhs;
                                          const auto cost = m_best.at(lhs).cost + m_best.at(rhs).cost + rows(subset);
                                          if (const auto it = m_best.find(subset); it == m_best.end() || cost < it->second.cost)
                                              m_best.insert_or_assign(subset, Split { cost, lhs });
                                          return true;
                                      });
    std::vector<std::uint64_t> components;
    for (auto unseen = bit::lo_set<std::uint64_t>[count]; unseen;)
    {
        auto component = unseen & -unseen;
        for (std::uint64_t previous = 0; component != previous;)
        {
            previous = component;
            bit::for_each_set_bit(previous, [&](size_t atom) { component |= graph[atom]; });
        }
        components.push_back(component);
        unseen &= ~component;
    }
    // Ascending sizes minimize C_out over left-deep sequences of cross products.
    std::ranges::stable_sort(components, {}, [&](std::uint64_t component) { return rows(component); });
    auto root = tree(components.front());
    auto joined = components.front();
    for (const auto component : std::span(components).subspan(1))
    {
        joined |= component;
        root = add({ joined, root, tree(component) });
    }
    m_refined.resize(m_tree.size());
    return build(root);
}
}
