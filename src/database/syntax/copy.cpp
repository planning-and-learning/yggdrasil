/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/syntax/query.hpp"

namespace ygg::database
{
std::vector<size_t> input_slots(std::span<const QueryView<>> roots)
{
    auto slots = std::vector<size_t>();
    for (const auto query : reachable(roots))
        if (is<QueryInputTag>(query))
            slots.push_back(as<QueryInputTag>(query).get_input_slot());
    std::ranges::sort(slots);
    const auto [first, last] = std::ranges::unique(slots);
    slots.erase(first, last);
    return slots;
}
}

namespace ygg::database
{
QueryPlan compile(std::span<const QueryView<>> roots)
{
    if (roots.empty())
        return QueryPlan {};
    auto repository = roots.front().get_repository().get_factory().create_shared();
    QueryBuilder builder;
    std::vector<Index<Query<>>> outputs;
    for (const auto copied : database::copy(roots, [](size_t slot) { return slot; }, *repository, builder))
        outputs.push_back(copied.get_index());
    return QueryPlan(std::move(repository), std::move(outputs));
}
}

namespace ygg::database
{
QueryPlan compile(std::initializer_list<QueryView<>> roots) { return compile(std::span<const QueryView<>>(roots)); }
}
