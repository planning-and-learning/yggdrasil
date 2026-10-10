/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_OPTIMIZATION_DETAILS_COST_HPP_
#define YGG_DATABASE_OPTIMIZATION_DETAILS_COST_HPP_

#include "yggdrasil/core/config.hpp"
#include "yggdrasil/database/optimization/details/dpccp.hpp"
#include "yggdrasil/database/optimization/details/estimation.hpp"
#include "yggdrasil/database/optimization/details/join_block.hpp"

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

/// Data-driven planning of a join block whose atoms all have estimates.
namespace ygg::database::optimization_detail
{
/// The most atoms DPccp's bitsets represent.
constexpr size_t max_cost_based_atoms = 63;

template<ColumnTypes Values>
class CostBasedPlanner
{
    struct Split
    {
        double cost;
        std::uint64_t lhs;
    };
    const JoinBlock<Values>* m_block;
    BlockBuilder<Values>* m_build;
    std::vector<std::vector<Index<Column>>> m_variables;
    std::vector<Index<Column>> m_required;
    std::map<std::uint64_t, Estimate> m_estimates;
    /// C_out of each subset's best plan and its left part; leaves cost nothing.
    std::map<std::uint64_t, Split> m_best;

    std::vector<size_t> atoms(std::uint64_t subset) const
    {
        std::vector<size_t> result;
        for (auto rest = subset; rest; rest &= rest - 1)
            result.push_back(static_cast<size_t>(std::countr_zero(rest)));
        return result;
    }
    std::vector<ColumnLayout> columns(std::uint64_t subset) const
    {
        std::vector<ColumnLayout> result;
        for (const auto atom : atoms(subset))
            for (const auto& column : m_block->atoms[atom].columns())
                if (!has(result, column.label))
                    result.push_back(column);
        return result;
    }
    const Estimate& estimate(std::uint64_t subset)
    {
        if (const auto it = m_estimates.find(subset); it != m_estimates.end())
            return it->second;
        const auto lowest = subset & -subset;
        auto combined = combine(estimate(lowest), estimate(subset ^ lowest));
        return m_estimates.emplace(subset, conjunction(std::move(combined), columns(subset))).first->second;
    }
    double rows(std::uint64_t subset) { return estimate(subset).rows; }
    /// Variables of the subset still needed outside it: output and join variables.
    std::vector<Index<Column>> needed(std::uint64_t subset) const
    {
        std::vector<Index<Column>> result = m_required;
        for (size_t atom = 0; atom < m_variables.size(); ++atom)
            if (!(subset >> atom & 1))
                result.insert(result.end(), m_variables[atom].begin(), m_variables[atom].end());
        return sorted(std::move(result));
    }
    /// A node of the chosen operator tree; leaves are single operands.
    struct Node
    {
        std::uint64_t subset;
        std::optional<size_t> lhs, rhs;
    };
    /// A refined node: a multi-way join over the given tree nodes, or unchanged.
    struct Refined
    {
        bool multiway = false;
        std::vector<size_t> inputs;
    };
    std::vector<Node> m_tree;
    std::vector<std::optional<Refined>> m_refined;

    size_t add(Node node)
    {
        m_tree.push_back(node);
        return m_tree.size() - 1;
    }
    /// The DPccp plan of a connected subset as tree nodes.
    size_t tree(std::uint64_t subset)
    {
        if (std::has_single_bit(subset))
            return add({ subset, {}, {} });
        const auto lhs = m_best.at(subset).lhs;
        const auto left = tree(lhs), right = tree(subset ^ lhs);
        return add({ subset, left, right });
    }
    /// Algorithm 4 of M. Freitag et al., "Adopting Worst-Case Optimal Joins in Relational
    /// Database Systems", PVLDB 13(11), 2020: bottom-up, a join whose estimate exceeds both
    /// inputs, or one with a multi-way input, is collapsed with its inputs into a single
    /// multi-way join, so a growing join and all its ancestors become one multi-way join.
    const Refined& refine(size_t node)
    {
        if (m_refined[node])
            return *m_refined[node];
        Refined result;
        const auto [subset, lhs, rhs] = m_tree[node];
        if (lhs)
        {
            const auto& left = refine(*lhs);
            const auto& right = refine(*rhs);
            const auto inputs = std::max(rows(m_tree[*lhs].subset), rows(m_tree[*rhs].subset));
            const bool growing = rows(subset) - inputs > FloatTolerance<float_t>::tolerance(rows(subset), inputs);
            if (growing || left.multiway || right.multiway)
            {
                std::vector<size_t> inputs;
                for (const auto [child, refined] : { std::pair(*lhs, &left), std::pair(*rhs, &right) })
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
    /// Builds a tree node, projecting it to the variables still needed.
    QueryView<Values> build(size_t node)
    {
        const auto [subset, lhs, rhs] = m_tree[node];
        const auto keep = needed(subset);
        if (!lhs)
            return m_build->restrict(m_block->atoms[std::countr_zero(subset)], keep);
        // Multi-way joins with only two inputs are built as binary joins (Freitag et al.).
        const auto refined = refine(node);
        if (!refined.multiway || refined.inputs.size() == 2)
            return m_build->restrict(m_build->join(build(*lhs), build(*rhs)), keep);
        std::vector<QueryView<Values>> inputs;
        for (const auto input : refined.inputs)
            inputs.push_back(build(input));
        return m_build->restrict(m_build->generic_join(inputs, variable_order<Values>(inputs, {})), keep);
    }

public:
    template<std::invocable<QueryView<Values>> GetEstimate>
    CostBasedPlanner(const JoinBlock<Values>& block, BlockBuilder<Values>& build, GetEstimate&& estimate_of) :
        m_block(&block),
        m_build(&build),
        m_required(sorted(detail::query_labels(block.output)))
    {
        for (size_t atom = 0; atom < block.atoms.size(); ++atom)
        {
            m_variables.push_back(variables(block.atoms[atom]));
            m_estimates.emplace(std::uint64_t { 1 } << atom, estimate_of(block.atoms[atom]));
            m_best.emplace(std::uint64_t { 1 } << atom, Split { 0, 0 });
        }
    }

    /// DPccp (G. Moerkotte, T. Neumann, VLDB 2006) under C_out, the sum of intermediate
    /// result sizes (S. Cluet, G. Moerkotte, ICDT 1995; V. Leis et al., PVLDB 2015), with
    /// System R estimates; connected components are joined last by cross products in
    /// ascending estimated size; then the tree is refined by Algorithm 4 of Freitag et al.
    QueryView<Values> plan()
    {
        const auto count = m_block->atoms.size();
        std::vector<std::uint64_t> graph(count);
        for (size_t lhs = 0; lhs < count; ++lhs)
            for (size_t rhs = lhs + 1; rhs < count; ++rhs)
                if (shares(m_variables[lhs], m_variables[rhs]))
                {
                    graph[lhs] |= std::uint64_t { 1 } << rhs;
                    graph[rhs] |= std::uint64_t { 1 } << lhs;
                }
        detail::enumerate_connected_pairs(graph,
                                          [&](std::uint64_t lhs, std::uint64_t rhs)
                                          {
                                              const auto subset = lhs | rhs;
                                              const auto cost = m_best.at(lhs).cost + m_best.at(rhs).cost + rows(subset);
                                              if (const auto it = m_best.find(subset); it == m_best.end() || cost < it->second.cost)
                                                  m_best.insert_or_assign(subset, Split { cost, lhs });
                                              return true;
                                          });
        std::vector<std::uint64_t> components;
        for (auto unseen = (std::uint64_t { 1 } << count) - 1; unseen;)
        {
            auto component = unseen & -unseen;
            for (std::uint64_t previous = 0; component != previous;)
            {
                previous = component;
                for (const auto atom : atoms(component))
                    component |= graph[atom];
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
};
}  // namespace ygg::database::optimization_detail

#endif
