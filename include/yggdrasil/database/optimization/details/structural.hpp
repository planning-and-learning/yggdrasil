/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_OPTIMIZATION_DETAILS_STRUCTURAL_HPP_
#define YGG_DATABASE_OPTIMIZATION_DETAILS_STRUCTURAL_HPP_

#include "yggdrasil/database/optimization/details/join_block.hpp"

#include <algorithm>
#include <optional>
#include <vector>

/// Data-independent planning of a join block from its hypergraph alone.
namespace ygg::database::optimization_detail
{
/// GYO reduction (M. H. Graham, "On the Universal Relation", 1979; C. T. Yu and
/// M. Z. Özsoyoğlu, "An Algorithm for Tree-Query Membership of a Distributed Query",
/// COMPSAC 1979): repeatedly drop variables occurring in a single edge and edges
/// contained in another edge. The hypergraph is acyclic iff one edge remains; each
/// dropped edge's container is its parent in a join tree.
inline std::optional<std::vector<std::optional<size_t>>> join_tree(std::vector<std::vector<Index<Column>>> edges)
{
    const auto count = edges.size();
    std::vector<std::optional<size_t>> parent(count);
    std::vector<bool> alive(count, true);
    size_t remaining = count;
    for (bool changed = true; changed && remaining > 1;)
    {
        changed = false;
        for (size_t edge = 0; edge < count; ++edge)
        {
            if (!alive[edge])
                continue;
            const auto removed = std::erase_if(edges[edge],
                                               [&](Index<Column> variable)
                                               {
                                                   for (size_t other = 0; other < count; ++other)
                                                       if (other != edge && alive[other] && std::ranges::binary_search(edges[other], variable))
                                                           return false;
                                                   return true;
                                               });
            changed |= removed != 0;
        }
        for (size_t edge = 0; edge < count && remaining > 1; ++edge)
        {
            if (!alive[edge])
                continue;
            for (size_t other = 0; other < count; ++other)
                if (other != edge && alive[other] && std::ranges::includes(edges[other], edges[edge]))
                {
                    parent[edge] = other;
                    alive[edge] = false;
                    --remaining;
                    changed = true;
                    break;
                }
        }
    }
    if (remaining > 1)
        return std::nullopt;
    return parent;
}

/// Yannakakis (M. Yannakakis, "Algorithms for Acyclic Database Schemes", VLDB 1981):
/// a bottom-up and a top-down semijoin pass remove dangling tuples, then joins along
/// the join tree keep only output variables and variables shared with the parent.
/// The result has the block's output variables in schema order.
template<ColumnTypes Values>
QueryView<Values> yannakakis(const JoinBlock<Values>& block, std::span<const std::optional<size_t>> parent, OperatorBuilder<Values>& build)
{
    const auto count = block.atoms.size();
    std::vector<std::vector<size_t>> children(count);
    size_t root = 0;
    for (size_t atom = 0; atom < count; ++atom)
        if (parent[atom])
            children[*parent[atom]].push_back(atom);
        else
            root = atom;
    std::vector<size_t> preorder { root };
    for (size_t position = 0; position < preorder.size(); ++position)
        preorder.insert(preorder.end(), children[preorder[position]].begin(), children[preorder[position]].end());

    auto relations = block.atoms;
    for (auto atom = preorder.rbegin(); atom != preorder.rend(); ++atom)
        if (parent[*atom])
            relations[*parent[*atom]] = build.semijoin(relations[*parent[*atom]], relations[*atom]);
    for (const auto atom : preorder)
        if (parent[atom])
            relations[atom] = build.semijoin(relations[atom], relations[*parent[atom]]);

    const auto required = ygg::canonicalized(column_labels(block.output.span()));
    std::vector<std::optional<QueryView<Values>>> results(count);
    for (auto atom = preorder.rbegin(); atom != preorder.rend(); ++atom)
    {
        auto joined = relations[*atom];
        for (const auto child : children[*atom])
            joined = build.join(joined, *results[child]);
        auto keep = required;
        if (parent[*atom])
        {
            const auto shared = variables(block.atoms[*parent[*atom]]);
            keep.insert(keep.end(), shared.begin(), shared.end());
            keep = ygg::canonicalized(std::move(keep));
        }
        results[*atom] = build.restrict(joined, keep);
    }
    return *results[root];
}

/// Plans a block from structure: Yannakakis if acyclic, otherwise one Generic Join
/// (H. Q. Ngo, C. Ré, A. Rudra, "Skew Strikes Back", SIGMOD Record 2013), whose
/// running time is bounded by the AGM bound of the full join (A. Atserias, M. Grohe,
/// D. Marx, FOCS 2008) for every variable order; variables are ordered by label.
template<ColumnTypes Values>
QueryView<Values> structural_plan(const JoinBlock<Values>& block, OperatorBuilder<Values>& build)
{
    std::vector<std::vector<Index<Column>>> edges;
    for (const auto atom : block.atoms)
        edges.push_back(variables(atom));
    if (const auto parent = join_tree(std::move(edges)))
        return yannakakis(block, *parent, build);
    return build.generic_join(block.atoms, variable_order<Values>(block.atoms, {}));
}
}  // namespace ygg::database::optimization_detail

#endif
