/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/details/min_distance.hpp"
#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>

namespace ygg::database::detail
{
urd::Store<urd::sequence<size_t>> column_range(size_t first, size_t last)
{
    auto positions = urd::Store<urd::sequence<size_t>>();
    for (auto c = first; c < last; ++c)
        positions.push_back(c);
    return positions;
}
}

namespace ygg::database::detail
{
QueryView<> checked_edges(QueryView<> edges, const DistancePlan& plan)
{
    require_plan_columns(edges.columns(), plan.edge_columns().span());
    return edges;
}
}
