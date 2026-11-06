/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_OPTIMIZATION_DETAILS_JOIN_BLOCK_HPP_
#define YGG_DATABASE_OPTIMIZATION_DETAILS_JOIN_BLOCK_HPP_

#include "yggdrasil/database/syntax/operator_builder.hpp"
#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>
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
struct JoinBlock
{
    urd::Store<urd::sequence<QueryView<>>> atoms;
    /// The output variables as a sorted set.
    urd::Store<urd::sequence<urd::index<Column>>> required;
};

urd::Store<urd::sequence<urd::index<Column>>> variables(QueryView<> query);

/// R ⋉ S, written R ⋈ π_common(S) with the existing operators, as Yannakakis' semijoin programs use it.
QueryView<> semijoin(OperatorBuilder& build, QueryView<> lhs, QueryView<> rhs);

/// The atoms' variables ordered by label.
urd::Store<urd::sequence<urd::index<Column>>> variable_order(std::span<const QueryView<>> atoms);
}  // namespace ygg::database::detail

#endif
