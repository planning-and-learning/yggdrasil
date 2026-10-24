/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/syntax/query.hpp"

namespace ygg::database::detail
{
QueryView<> clone_query(QueryView<> source, std::span<const QueryView<>> children, QueryRepository& destination, QueryBuilder& builder)
{
    std::vector<QueryView<>> original;
    for_each_child(source, [&](QueryView<> child) { original.push_back(child); });
    if (original.size() != children.size())
        throw std::invalid_argument("Query clone: wrong child count.");
    for (size_t i = 0; i < children.size(); ++i)
    {
        if (&children[i].get_repository() != &destination)
            throw std::invalid_argument("Query clone: children must belong to the destination.");
        require_plan_columns(children[i].columns(), original[i].columns());
    }
    return ygg::visit(
        [&]<typename Tag>(QueryView<Tag> concrete)
        {
            size_t child = 0;
            auto data = ygg::map_children<TypeList<Query<>>>(concrete, [&](QueryView<>) { return children[child++]; });
            return insert_query(destination, builder, data);
        },
        source.get_variant());
}
}

namespace ygg::database
{
std::vector<QueryView<>> reachable(std::span<const QueryView<>> roots)
{
    if (roots.empty())
        return {};
    const auto& repository = roots.front().get_repository();
    std::vector<bool> seen(repository.size());
    std::vector<QueryView<>> pending;
    for (const auto root : roots)
    {
        if (&root.get_repository() != &repository || root.get_index().get_value() >= repository.size())
            throw std::invalid_argument("Query: roots must belong to one repository.");
        pending.push_back(root);
    }
    while (!pending.empty())
    {
        const auto query = pending.back();
        pending.pop_back();
        if (seen[query.get_index().get_value()])
            continue;
        seen[query.get_index().get_value()] = true;
        for_each_child(query, [&](QueryView<> child) { pending.push_back(child); });
    }
    std::vector<QueryView<>> result;
    for (size_t value = 0; value < seen.size(); ++value)
        if (seen[value])
            result.emplace_back(Index<Query<>>(to_uint_t(value)), repository);
    return result;
}
}
