/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/optimization/detail/structural.hpp"

#include "yggdrasil/core/config.hpp"

namespace ygg::database::detail
{
std::optional<std::vector<std::optional<size_t>>> join_tree(std::vector<std::vector<Index<Column>>> edges)
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
}

namespace ygg::database::detail
{
QueryView<> yannakakis(const JoinBlock& block, std::span<const std::optional<size_t>> parent, OperatorBuilder& build)
{
    const auto count = block.atoms.size();
    std::vector<std::vector<size_t>> children(count);
    size_t root = 0;
    for (size_t atom = 0; atom < count; ++atom)
        if (const auto& above = parent[atom])
            children[*above].push_back(atom);
        else
            root = atom;
    std::vector<size_t> preorder { root };
    for (size_t position = 0; position < preorder.size(); ++position)
        for (const auto child : children[preorder[position]])
            preorder.push_back(child);

    auto relations = block.atoms;
    for (auto atom = preorder.rbegin(); atom != preorder.rend(); ++atom)
        if (const auto& above = parent[*atom])
            relations[*above] = semijoin(build, relations[*above], relations[*atom]);
    for (const auto atom : preorder)
        if (const auto& above = parent[atom])
            relations[atom] = semijoin(build, relations[atom], relations[*above]);

    std::vector<std::optional<QueryView<>>> results(count);
    for (auto atom = preorder.rbegin(); atom != preorder.rend(); ++atom)
    {
        auto joined = relations[*atom];
        for (const auto child : children[*atom])
        {
            const auto& result = results[child];
            YGG_ASSUME(result.has_value());  // the reverse preorder visits children first
            joined = build.join(joined, *result);
        }
        auto keep = block.required;
        if (const auto& above = parent[*atom])
        {
            const auto shared = variables(block.atoms[*above]);
            keep.insert(keep.end(), shared.begin(), shared.end());
            keep = ygg::canonicalized(std::move(keep));
        }
        results[*atom] = build.restrict(joined, keep);
    }
    const auto& result = results[root];
    YGG_ASSUME(result.has_value());  // the reverse preorder ends at the root
    return *result;
}
}

namespace ygg::database::detail
{
QueryView<> structural_plan(const JoinBlock& block, OperatorBuilder& build)
{
    std::vector<std::vector<Index<Column>>> edges;
    edges.reserve(block.atoms.size());
    for (const auto atom : block.atoms)
        edges.push_back(variables(atom));
    if (const auto parent = join_tree(std::move(edges)))
        return yannakakis(block, *parent, build);
    return build.generic_join(block.atoms, variable_order(block.atoms));
}
}
