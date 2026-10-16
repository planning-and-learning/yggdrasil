/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_OPTIMIZATION_DETAILS_JOIN_BLOCK_HPP_
#define YGG_DATABASE_OPTIMIZATION_DETAILS_JOIN_BLOCK_HPP_

#include "yggdrasil/database/syntax/operator_builder.hpp"
#include "yggdrasil/database/syntax/query.hpp"
#include "yggdrasil/semantics/canonicalization.hpp"

#include <algorithm>
#include <concepts>
#include <span>
#include <vector>

namespace ygg::database::detail
{
/// A natural join of atoms whose output keeps the required variables: atom_1 ⋈ … ⋈ atom_n.
/// Atoms are planned operands; columns with equal labels are join variables.
template<ColumnTypes Values>
struct JoinBlock
{
    std::vector<QueryView<Values>> atoms;
    /// The output variables as a sorted set.
    std::vector<Index<Column>> required;
};

template<ColumnTypes Values>
std::vector<Index<Column>> variables(QueryView<Values> query)
{
    return ygg::canonicalized(column_labels(query.columns()));
}

/// R ⋉ S, written R ⋈ π_common(S) with the existing operators, as Yannakakis' semijoin programs use it.
template<ColumnTypes Values>
QueryView<Values> semijoin(OperatorBuilder<Values>& build, QueryView<Values> lhs, QueryView<Values> rhs)
{
    return build.join(lhs, build.restrict(rhs, variables(lhs)));
}

/// The atoms' variables ordered by label.
template<ColumnTypes Values>
std::vector<Index<Column>> variable_order(std::span<const QueryView<Values>> atoms)
{
    std::vector<Index<Column>> result;
    for (const auto atom : atoms)
        for (const auto label : variables(atom))
            result.push_back(label);
    ygg::canonicalize(result);
    return result;
}
}  // namespace ygg::database::detail

#endif
