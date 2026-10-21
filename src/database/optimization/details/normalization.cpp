/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/optimization/details/normalization.hpp"

namespace ygg::database::detail
{
QueryView<> Normalizer::project(QueryView<> arg, std::span<const Index<Column>> columns)
{
    if (std::ranges::equal(column_labels(arg.columns()), columns))
        return arg;
    if (is<QueryProjectTag>(arg))
        return project(as<QueryProjectTag>(arg).get_arg(), columns);
    if (is<QueryUnionTag>(arg))
    {
        const auto operation = as<QueryUnionTag>(arg);
        return union_(project(operation.get_lhs(), columns), project(operation.get_rhs(), columns));
    }
    if (is<QueryRenameTag>(arg))
    {
        const auto operation = as<QueryRenameTag>(arg);
        const auto inner = inverse(operation, columns);
        return rename(project(operation.get_arg(), inner), columns);
    }
    // Into the lhs of a semijoin or antijoin, keeping the labels it shares with the rhs.
    if (is<QuerySemijoinTag>(arg))
        return project_filter_join<QuerySemijoinTag>(arg, columns);
    if (is<QueryAntijoinTag>(arg))
        return project_filter_join<QueryAntijoinTag>(arg, columns);
    return finish_project(arg, columns);
}
}
