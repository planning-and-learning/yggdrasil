/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_OPTIMIZATION_DETAILS_ESTIMATION_HPP_
#define YGG_DATABASE_OPTIMIZATION_DETAILS_ESTIMATION_HPP_

#include "yggdrasil/containers/associative_containers.hpp"
#include "yggdrasil/database/optimization/statistics.hpp"
#include "yggdrasil/database/syntax/query.hpp"

#include <algorithm>
#include <cmath>
#include <compare>
#include <concepts>
#include <limits>
#include <optional>
#include <stdexcept>
#include <variant>
#include <vector>

/// Cardinality estimation of System R: P. G. Selinger et al., "Access Path Selection
/// in a Relational Database Management System", SIGMOD 1979. Columns are independent
/// and uniform, join columns satisfy containment, and columns equated by joins or
/// selections form one class whose distinct count is the class minimum. Other
/// operators follow Garcia-Molina, Ullman, and Widom (see below).
namespace ygg::database::detail
{
struct InputFactorIdentity
{
    size_t value;
    auto operator<=>(const InputFactorIdentity&) const = default;
};
struct QueryFactorIdentity
{
    size_t value;
    auto operator<=>(const QueryFactorIdentity&) const = default;
};
/// Identical factors with identical column bindings count once, so A ⋈ A estimates as A.
using FactorIdentity = std::variant<InputFactorIdentity, QueryFactorIdentity>;
/// A column of a factor: its position in the factor and its current label.
struct Attribute
{
    size_t position;
    Index<Column> column;
    double distinct;
    auto operator<=>(const Attribute&) const = default;
};
struct Factor
{
    FactorIdentity identity;
    double rows;
    std::vector<Attribute> attributes;
};
/// A conjunction of factors, equalities, and constants, independent of join order.
struct Estimate
{
    double rows = 0;
    UnorderedMap<Index<Column>, double> distinct;
    std::vector<Factor> factors;
    Set<std::pair<Index<Column>, Index<Column>>> equalities;
    Set<std::pair<Index<Column>, std::vector<std::byte>>> constants;
};

/// Estimates saturate here instead of overflowing to infinity.
constexpr double max_estimate = 1e300;
/// exp() of larger logarithms overflows a double.
constexpr double max_log_estimate = 690;
inline double bounded(double value) { return std::clamp(value, 0.0, max_estimate); }
inline double multiply(double a, double b) { return a == 0 || b == 0 ? 0 : bounded(a * b); }

Estimate factor(const RelationStatistics& stats, std::span<const ColumnLayout> columns, FactorIdentity identity);

/// The conjunction's rows: the product of distinct factors divided, per column class,
/// by all but the smallest distinct count (one factor of a constant class), as in
/// System R's |R ⋈ S| = |R|·|S| / max(V(R,A), V(S,A)).
Estimate conjunction(Estimate result, std::span<const Index<Column>> labels);

Estimate combine(Estimate lhs, const Estimate& rhs);

void validate_statistics(const RelationStatistics& stats, std::span<const ColumnLayout> columns);

/// A missing distinct count is the row count, capped by a bounded domain.
RelationStatistics with_default_distinct(RelationStatistics stats, std::span<const ColumnLayout> columns, std::optional<size_t> objects);

template<class GetEstimate>
Estimate estimate_operation(QueryView<QueryInputTag> query, FactorIdentity, GetEstimate&&, const Statistics& statistics)
{
    const auto it = statistics.inputs.find(query.get_input_slot());
    if (it == statistics.inputs.end())
        throw std::logic_error("Optimizer: estimates require the input's row count.");
    return factor(with_default_distinct(it->second, query.columns(), statistics.objects), query.columns(), InputFactorIdentity { query.get_input_slot() });
}
template<class GetEstimate>
Estimate estimate_operation(QueryView<QueryEmptyTag> query, FactorIdentity identity, GetEstimate&&, const Statistics&)
{
    return factor({ 0, {} }, query.columns(), identity);
}
template<class GetEstimate>
Estimate estimate_operation(QueryView<QueryJoinTag> query, FactorIdentity, GetEstimate child, const Statistics&)
{
    return conjunction(combine(child(query.get_lhs()), child(query.get_rhs())), column_labels(query.columns()));
}
template<class GetEstimate>
Estimate estimate_operation(QueryView<QueryGenericJoinTag> query, FactorIdentity identity, GetEstimate child, const Statistics&)
{
    const auto inputs = query.get_inputs();
    if (inputs.empty())
        return factor({ 1, {} }, query.columns(), identity);
    auto result = child(QueryView<>(inputs.front(), query.get_repository()));
    for (const auto input : inputs.subspan(1))
        result = combine(std::move(result), child(QueryView<>(input, query.get_repository())));
    return conjunction(std::move(result), column_labels(query.columns()));
}
template<class GetEstimate>
Estimate estimate_operation(QueryView<QuerySelectEqualTag> query, FactorIdentity, GetEstimate child, const Statistics&)
{
    auto result = child(query.get_arg());
    const auto lhs = query.get_lhs_column(), rhs = query.get_rhs_column();
    result.equalities.emplace(std::min(lhs, rhs), std::max(lhs, rhs));
    return conjunction(std::move(result), column_labels(query.columns()));
}
template<class GetEstimate>
Estimate estimate_operation(QueryView<QuerySelectValueTag> query, FactorIdentity, GetEstimate child, const Statistics&)
{
    auto result = child(query.get_arg());
    const auto constant = query.get_constant();
    result.constants.emplace(query.get_column(), std::vector<std::byte>(constant.begin(), constant.end()));
    return conjunction(std::move(result), column_labels(query.columns()));
}
template<class GetEstimate>
Estimate estimate_operation(QueryView<QueryRenameTag> query, FactorIdentity, GetEstimate child, const Statistics&)
{
    auto result = child(query.get_arg());
    UnorderedMap<Index<Column>, Index<Column>> renamed;
    const auto before = query.get_arg().columns(), after = query.columns();
    for (size_t i = 0; i < before.size(); ++i)
        renamed[before[i].label] = after[i].label;
    for (auto& single : result.factors)
        for (auto& attribute : single.attributes)
            attribute.column = renamed.at(attribute.column);
    Set<std::pair<Index<Column>, Index<Column>>> equalities;
    for (const auto& [lhs, rhs] : result.equalities)
        equalities.emplace(std::min(renamed.at(lhs), renamed.at(rhs)), std::max(renamed.at(lhs), renamed.at(rhs)));
    Set<std::pair<Index<Column>, std::vector<std::byte>>> constants;
    for (const auto& [column, value] : result.constants)
        constants.emplace(renamed.at(column), value);
    result.equalities = std::move(equalities);
    result.constants = std::move(constants);
    return conjunction(std::move(result), column_labels(after));
}
/// Operators beyond System R follow the textbook estimates of Garcia-Molina, Ullman,
/// and Widom (Database Systems: The Complete Book, 2nd ed., 2008, §16.4), with distinct
/// counts under containment of value sets and capped by the result size.
RelationStatistics capped(RelationStatistics stats);
/// Duplicate elimination after projection: min(T(R)/2, ∏ V(R, a)) over the kept columns.
/// A projection that keeps every column only reorders it.
template<class GetEstimate>
Estimate estimate_operation(QueryView<QueryProjectTag> query, FactorIdentity identity, GetEstimate child, const Statistics&)
{
    auto argument = child(query.get_arg());
    if (query.columns().size() == query.get_arg().columns().size())
        return argument;
    RelationStatistics result { 0, {} };
    double combinations = 1;
    for (const auto& column : query.columns())
    {
        result.distinct[column.label] = argument.distinct.at(column.label);
        combinations = multiply(combinations, argument.distinct.at(column.label));
    }
    result.rows = std::min(argument.rows / 2, combinations);
    return factor(capped(std::move(result)), query.columns(), identity);
}
/// Set union: max(T(R), T(S)) + min(T(R), T(S))/2; V = max(V(R, a), V(S, a)).
template<class GetEstimate>
Estimate estimate_operation(QueryView<QueryUnionTag> query, FactorIdentity identity, GetEstimate child, const Statistics&)
{
    const auto lhs = child(query.get_lhs()), rhs = child(query.get_rhs());
    RelationStatistics result { bounded(std::max(lhs.rows, rhs.rows) + std::min(lhs.rows, rhs.rows) / 2), {} };
    for (const auto& [column, count] : lhs.distinct)
        result.distinct[column] = std::max(count, rhs.distinct.at(column));
    return factor(capped(std::move(result)), query.columns(), identity);
}
/// Difference: T(R) − T(S)/2; V = V(R, a).
template<class GetEstimate>
Estimate estimate_operation(QueryView<QueryDifferenceTag> query, FactorIdentity identity, GetEstimate child, const Statistics&)
{
    const auto lhs = child(query.get_lhs()), rhs = child(query.get_rhs());
    return factor(capped({ std::max(0.0, lhs.rows - rhs.rows / 2), lhs.distinct }), query.columns(), identity);
}
/// Semijoin: T(R ⋉ S) = min(T(R), T(π_R(R ⋈ S))); antijoin: T(R ▷ S) = T(R) − T(R ⋉ S); V = V(R, a).
template<typename Tag, class GetEstimate>
    requires(std::same_as<Tag, QuerySemijoinTag> || std::same_as<Tag, QueryAntijoinTag>)
Estimate estimate_operation(QueryView<Tag> query, FactorIdentity identity, GetEstimate child, const Statistics&)
{
    const auto lhs = child(query.get_lhs());
    const auto matched = std::min(lhs.rows, conjunction(combine(lhs, child(query.get_rhs())), column_labels(query.columns())).rows);
    const auto rows = std::same_as<Tag, QuerySemijoinTag> ? matched : std::max(0.0, lhs.rows - matched);
    return factor(capped({ rows, lhs.distinct }), query.columns(), identity);
}
/// The literature has no estimate for distances; they are only known when observed.
template<class GetEstimate>
Estimate estimate_operation(QueryView<QueryDistanceTag>, FactorIdentity, GetEstimate&&, const Statistics&)
{
    throw std::logic_error("Optimizer: distances have no estimate unless observed.");
}

/// The System R estimate of a query from its children's estimates. An observation of
/// the equivalent caller query replaces the derivation (LEO, VLDB 2001).
template<std::invocable<QueryView<>> GetEstimate>
Estimate estimate(QueryView<> query, std::type_identity_t<std::optional<Index<Query<>>>> source, GetEstimate child, const Statistics& statistics)
{
    const FactorIdentity identity = QueryFactorIdentity { query.get_index().get_value() };
    if (source)
        if (const auto it = statistics.expressions.find(*source); it != statistics.expressions.end())
            return factor(with_default_distinct(it->second, query.columns(), statistics.objects), query.columns(), identity);
    return ygg::visit([&]<typename Child>(Child operation) { return estimate_operation(operation, identity, child, statistics); }, query.get_variant());
}
}  // namespace ygg::database::detail

#endif
