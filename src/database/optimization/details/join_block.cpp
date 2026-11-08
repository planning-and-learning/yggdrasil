/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/optimization/details/join_block.hpp"
#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>

namespace ygg::database::detail
{
urd::Store<urd::sequence<urd::index<Column>>> variables(QueryView<> query)
{
    auto labels = urd::Store<urd::sequence<urd::index<Column>>>();
    for (const auto& column : query.columns())
        labels.push_back(column.label);
    return ygg::canonicalized(std::move(labels));
}
}

namespace ygg::database::detail
{
QueryView<> semijoin(OperatorBuilder& build, QueryView<> lhs, QueryView<> rhs) { return build.join(lhs, build.restrict(rhs, variables(lhs))); }
}

namespace ygg::database::detail
{
urd::Store<urd::sequence<urd::index<Column>>> variable_order(std::span<const QueryView<>> atoms)
{
    urd::Store<urd::sequence<urd::index<Column>>> result;
    for (const auto atom : atoms)
        for (const auto label : variables(atom))
            result.push_back(label);
    ygg::canonicalize(result);
    return result;
}
}
