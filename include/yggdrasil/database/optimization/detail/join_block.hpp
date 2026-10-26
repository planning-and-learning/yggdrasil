/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_OPTIMIZATION_DETAIL_JOIN_BLOCK_HPP_
#define YGG_DATABASE_OPTIMIZATION_DETAIL_JOIN_BLOCK_HPP_

#include "yggdrasil/database/query/operator_builder.hpp"
#include "yggdrasil/database/query/query.hpp"
#include "yggdrasil/semantics/canonicalization.hpp"

#include <algorithm>
#include <concepts>
#include <span>
#include <vector>

namespace ygg::database::detail
{
/// A natural join of atoms whose output keeps the required variables: atom_1 ⋈ … ⋈ atom_n.
/// Atoms are planned operands; columns with equal labels are join variables.
struct JoinBlock
{
    std::vector<QueryView<>> atoms;
    /// The output variables as a sorted set.
    std::vector<Index<Column>> required;
};

std::vector<Index<Column>> variables(QueryView<> query);

/// R ⋉ S, written R ⋈ π_common(S) with the existing operators, as Yannakakis' semijoin programs use it.
QueryView<> semijoin(OperatorBuilder& build, QueryView<> lhs, QueryView<> rhs);

/// The atoms' variables ordered by label.
std::vector<Index<Column>> variable_order(std::span<const QueryView<>> atoms);
}  // namespace ygg::database::detail

#endif
