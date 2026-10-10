/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_OPTIMIZATION_OPTIMIZATION_HPP_
#define YGG_DATABASE_OPTIMIZATION_OPTIMIZATION_HPP_

#include "yggdrasil/database/optimization/details/optimization.hpp"
#include "yggdrasil/database/optimization/statistics.hpp"
#include "yggdrasil/database/semantics/operations.hpp"
#include "yggdrasil/database/semantics/relation_view.hpp"
#include "yggdrasil/database/syntax/query.hpp"

#include <concepts>
#include <ranges>
#include <span>
#include <type_traits>
#include <vector>

namespace ygg::database
{
/// Measures row and per-column distinct counts of the relations bound to each input slot.
template<ColumnTypes Values, RelationViewRange<Values> R>
Statistics<Values> collect_statistics(const R& inputs)
{
    Statistics<Values> result;
    size_t slot = 0;
    for (const auto& input : inputs)
    {
        auto& stats = result.inputs[slot++];
        stats.rows = static_cast<double>(input.size());
        const auto columns = input.columns();
        for (const auto& column : columns.span())
            stats.distinct[column.label] = static_cast<double>(project<Values>(input, { column.label }).size());
    }
    return result;
}

/// Plans the roots by applying established query optimization methods:
///
/// 1. The query is normalized by the textbook algebraic rewrites [GMUW08]: selections
///    are pushed down through joins, projections, renames, unions, and differences,
///    projections through renames and unions, and empty relations are propagated.
/// 2. Each maximal tree of joins forms a join block, planned as a whole. Unions,
///    differences, distances, and selections spanning several join operands bound
///    the blocks.
/// 3. A block whose operands all have known cardinalities is planned cost-based:
///    DPccp [MN06] finds the bushy join order of least C_out [CM95, Leis15] under
///    System R cardinality estimates [Selinger79] (the textbook estimates of [GMUW08]
///    for projection, union, and difference), with observed cardinalities replacing
///    estimates [LEO01]; disconnected components are joined last by cross products.
///    The join tree is then refined by Algorithm 4 of [Freitag20]: growing joins and
///    their ancestors become one worst-case-optimal join.
/// 4. Any other block is planned from its structure: if the GYO reduction
///    [Graham79, YO79] shows it acyclic, by Yannakakis' algorithm [Yannakakis81];
///    otherwise by Generic Join [NRR13], whose running time is bounded by the AGM
///    bound [AGM08].
///
/// Generic Join's variable order is fixed by column label: any order attains the
/// worst-case bound, but [Freitag20] additionally optimizes it with the Tributary Join
/// cost model, which is not implemented. Distances have no estimate in the literature
/// and are only known when observed. Not modeled: distributing joins over unions,
/// sharing-aware choices across roots, and costs of incremental maintenance.
///
/// The roots belong to one repository; the plan preserves every root's ordered, typed
/// schema and set semantics.
///
/// [Selinger79] P. G. Selinger et al., Access Path Selection in a Relational Database
///     Management System, SIGMOD 1979.
/// [GMUW08] H. Garcia-Molina, J. D. Ullman, J. Widom, Database Systems: The Complete
///     Book, 2nd ed., 2008, ch. 16.
/// [Graham79] M. H. Graham, On the Universal Relation, University of Toronto, 1979.
/// [YO79] C. T. Yu, M. Z. Özsoyoğlu, An Algorithm for Tree-Query Membership of a
///     Distributed Query, COMPSAC 1979.
/// [Yannakakis81] M. Yannakakis, Algorithms for Acyclic Database Schemes, VLDB 1981.
/// [AGM08] A. Atserias, M. Grohe, D. Marx, Size Bounds and Query Plans for Relational
///     Joins, FOCS 2008.
/// [NRR13] H. Q. Ngo, C. Ré, A. Rudra, Skew Strikes Back: New Developments in the
///     Theory of Join Algorithms, SIGMOD Record 2013.
/// [MN06] G. Moerkotte, T. Neumann, Analysis of Two Existing and One New Dynamic
///     Programming Algorithm for the Generation of Optimal Bushy Join Trees without
///     Cross Products, VLDB 2006.
/// [CM95] S. Cluet, G. Moerkotte, On the Complexity of Generating Optimal Left-Deep
///     Processing Trees with Cross Products, ICDT 1995.
/// [Leis15] V. Leis et al., How Good Are Query Optimizers, Really?, PVLDB 9(3), 2015.
/// [Freitag20] M. Freitag, M. Bandle, T. Schmidt, A. Kemper, T. Neumann, Adopting
///     Worst-Case Optimal Joins in Relational Database Systems, PVLDB 13(11), 2020.
/// [LEO01] M. Stillger, G. Lohman, V. Markl, M. Kandil, LEO – DB2's LEarning Optimizer,
///     VLDB 2001.
template<ColumnTypes Values>
QueryPlan<Values> optimize(std::span<const QueryView<Values>> roots, const std::type_identity_t<Statistics<Values>>& statistics = {})
{
    return detail::optimize(roots, statistics);
}

template<ColumnTypes Values>
QueryPlan<Values> optimize(QueryView<Values> root, const std::type_identity_t<Statistics<Values>>& statistics = {})
{
    return optimize(std::span<const QueryView<Values>>(&root, 1), statistics);
}

template<ColumnTypes Values, std::ranges::contiguous_range R>
    requires std::same_as<std::ranges::range_value_t<R>, QueryView<Values>>
QueryPlan<Values> optimize(const R& roots, const std::type_identity_t<Statistics<Values>>& statistics = {})
{
    return optimize(std::span<const QueryView<Values>>(roots), statistics);
}
}  // namespace ygg::database
#endif
