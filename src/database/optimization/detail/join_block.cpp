/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/optimization/detail/join_block.hpp"

namespace ygg::database::detail
{
std::vector<Index<Column>> variables(QueryView<> query) { return ygg::canonicalized(column_labels(query.columns())); }
}

namespace ygg::database::detail
{
QueryView<> semijoin(OperatorBuilder& build, QueryView<> lhs, QueryView<> rhs) { return build.join(lhs, build.restrict(rhs, variables(lhs))); }
}

namespace ygg::database::detail
{
std::vector<Index<Column>> variable_order(std::span<const QueryView<>> atoms)
{
    std::vector<Index<Column>> result;
    for (const auto atom : atoms)
        for (const auto label : variables(atom))
            result.push_back(label);
    ygg::canonicalize(result);
    return result;
}
}
