/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_OPTIMIZATION_DETAILS_ESTIMATION_HPP_
#define YGG_DATABASE_OPTIMIZATION_DETAILS_ESTIMATION_HPP_

#include "yggdrasil/containers/associative_containers.hpp"
#include "yggdrasil/database/syntax/query.hpp"

#include <algorithm>
#include <cmath>
#include <compare>
#include <concepts>
#include <limits>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <variant>
#include <vector>

/// Cardinality estimation of System R: P. G. Selinger et al., "Access Path Selection
/// in a Relational Database Management System", SIGMOD 1979. Columns are independent
/// and uniform, join columns satisfy containment, and columns equated by joins or
/// selections form one class whose distinct count is the class minimum. Other
/// operators follow Garcia-Molina, Ullman, and Widom (see below).
namespace ygg::database::optimization_detail
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
struct Factor
{
    FactorIdentity identity;
    double rows;
    std::vector<std::tuple<size_t, uint_t, double>> attributes;
};
/// A conjunction of factors, equalities, and constants, independent of join order.
struct Estimate
{
    double rows = 0;
    UnorderedMap<Index<Column>, double> distinct;
    std::vector<Factor> factors;
    Set<std::pair<uint_t, uint_t>> equalities;
    Set<std::pair<uint_t, std::vector<std::byte>>> constants;
};

/// Estimates saturate here instead of overflowing to infinity.
constexpr double max_estimate = 1e300;
/// exp() of larger logarithms overflows a double.
constexpr double max_log_estimate = 690;
inline double bounded(double value) { return std::clamp(value, 0.0, max_estimate); }
inline double multiply(double a, double b) { return a == 0 || b == 0 ? 0 : bounded(a * b); }

inline Estimate factor(const RelationStatistics& stats, std::span<const ColumnLayout> columns, FactorIdentity identity)
{
    Estimate result;
    result.rows = stats.rows;
    Factor single { identity, stats.rows, {} };
    for (size_t i = 0; i < columns.size(); ++i)
    {
        const auto label = columns[i].label;
        const auto found = stats.distinct.find(label);
        const auto ndv = std::min(stats.rows, found == stats.distinct.end() ? stats.rows : found->second);
        result.distinct[label] = ndv;
        single.attributes.emplace_back(i, label.get_value(), ndv);
    }
    result.factors.push_back(std::move(single));
    return result;
}

/// The conjunction's rows: the product of distinct factors divided, per column class,
/// by all but the smallest distinct count (one factor of a constant class), as in
/// System R's |R ⋈ S| = |R|·|S| / max(V(R,A), V(S,A)).
inline Estimate conjunction(Estimate result, std::span<const Index<Column>> labels)
{
    // ponytail: hand-rolled union-find over a few columns; boost::disjoint_sets needs property maps for no gain here.
    UnorderedMap<uint_t, uint_t> parent;
    for (const auto& single : result.factors)
        for (const auto& [slot, column, ndv] : single.attributes)
            parent[column] = column;
    const auto find = [&](uint_t column)
    {
        while (parent.at(column) != column)
            column = parent.at(column);
        return column;
    };
    for (const auto& [lhs, rhs] : result.equalities)
    {
        const auto left = find(lhs), right = find(rhs);
        parent[std::max(left, right)] = std::min(left, right);
    }
    UnorderedMap<uint_t, std::vector<std::byte>> bound;
    bool zero = false;
    for (const auto& [column, value] : result.constants)
    {
        const auto [it, inserted] = bound.emplace(find(column), value);
        if (!inserted && it->second != value)
            zero = true;
    }
    UnorderedMap<uint_t, std::vector<double>> domains;
    Set<std::pair<FactorIdentity, std::vector<std::pair<size_t, uint_t>>>> unique;
    double logarithm = 0;
    for (const auto& single : result.factors)
    {
        std::vector<std::pair<size_t, uint_t>> bindings;
        for (const auto& [slot, column, ndv] : single.attributes)
            bindings.emplace_back(slot, find(column));
        if (!unique.emplace(single.identity, std::move(bindings)).second)
            continue;
        if (single.rows == 0)
            zero = true;
        else
            logarithm += std::log(single.rows);
        for (const auto& [slot, column, ndv] : single.attributes)
            domains[find(column)].push_back(std::max(1.0, ndv));
    }
    for (const auto& [column, counts] : domains)
    {
        for (const auto count : counts)
            logarithm -= std::log(count);
        if (!bound.contains(column))
            logarithm += std::log(*std::ranges::min_element(counts));
    }
    result.rows = zero ? 0 : std::exp(std::clamp(logarithm, -max_log_estimate, max_log_estimate));
    result.distinct.clear();
    for (const auto label : labels)
    {
        const auto representative = find(label.get_value());
        const auto& counts = domains.at(representative);
        result.distinct[label] = std::min(result.rows, bound.contains(representative) ? 1.0 : *std::ranges::min_element(counts));
    }
    return result;
}

inline Estimate combine(Estimate lhs, const Estimate& rhs)
{
    for (const auto& single : rhs.factors)
        if (std::ranges::none_of(lhs.factors,
                                 [&](const auto& existing) { return existing.identity == single.identity && existing.attributes == single.attributes; }))
            lhs.factors.push_back(single);
    lhs.equalities.insert(rhs.equalities.begin(), rhs.equalities.end());
    lhs.constants.insert(rhs.constants.begin(), rhs.constants.end());
    return lhs;
}

inline void validate_statistics(const RelationStatistics& stats, std::span<const ColumnLayout> columns)
{
    if (!std::isfinite(stats.rows) || stats.rows < 0 || (columns.empty() && stats.rows > 1))
        throw std::invalid_argument("Optimizer: invalid row count.");
    for (const auto& [label, count] : stats.distinct)
        if (!contains_column(columns, label) || !std::isfinite(count) || count < 0 || count > stats.rows || (stats.rows > 0 && count < 1))
            throw std::invalid_argument("Optimizer: invalid distinct count or column.");
}

/// A missing distinct count is the row count, capped by a bounded domain.
template<ColumnTypes Values>
RelationStatistics with_default_distinct(RelationStatistics stats, std::span<const ColumnLayout> columns, const Statistics<Values>& statistics)
{
    const auto domain = statistics.objects ? static_cast<double>(*statistics.objects) : max_estimate;
    for (const auto& column : columns)
        stats.distinct.try_emplace(column.label, std::min(stats.rows, domain));
    return stats;
}

template<ColumnTypes Values, class GetEstimate>
Estimate estimate_operation(QueryView<Values, QueryInputTag> query, FactorIdentity, GetEstimate&&, const Statistics<Values>& statistics)
{
    const auto it = statistics.inputs.find(query.get_input_slot());
    if (it == statistics.inputs.end())
        throw std::logic_error("Optimizer: estimates require the input's row count.");
    return factor(with_default_distinct(it->second, query.columns(), statistics), query.columns(), InputFactorIdentity { query.get_input_slot() });
}
template<ColumnTypes Values, class GetEstimate>
Estimate estimate_operation(QueryView<Values, QueryEmptyTag> query, FactorIdentity identity, GetEstimate&&, const Statistics<Values>&)
{
    return factor({ 0, {} }, query.columns(), identity);
}
template<ColumnTypes Values, class GetEstimate>
Estimate estimate_operation(QueryView<Values, QueryJoinTag> query, FactorIdentity, GetEstimate&& child, const Statistics<Values>&)
{
    return conjunction(combine(child(query.get_lhs()), child(query.get_rhs())), column_labels(query.columns()));
}
template<ColumnTypes Values, class GetEstimate>
Estimate estimate_operation(QueryView<Values, QueryGenericJoinTag> query, FactorIdentity identity, GetEstimate&& child, const Statistics<Values>&)
{
    const auto inputs = query.get_inputs();
    if (inputs.empty())
        return factor({ 1, {} }, query.columns(), identity);
    auto result = child(QueryView<Values>(inputs.front(), query.get_repository()));
    for (const auto input : inputs.subspan(1))
        result = combine(std::move(result), child(QueryView<Values>(input, query.get_repository())));
    return conjunction(std::move(result), column_labels(query.columns()));
}
template<ColumnTypes Values, class GetEstimate>
Estimate estimate_operation(QueryView<Values, QuerySelectEqualTag> query, FactorIdentity, GetEstimate&& child, const Statistics<Values>&)
{
    auto result = child(query.get_arg());
    const auto lhs = query.get_lhs_column().get_value(), rhs = query.get_rhs_column().get_value();
    result.equalities.emplace(std::min(lhs, rhs), std::max(lhs, rhs));
    return conjunction(std::move(result), column_labels(query.columns()));
}
template<ColumnTypes Values, class GetEstimate>
Estimate estimate_operation(QueryView<Values, QuerySelectValueTag> query, FactorIdentity, GetEstimate&& child, const Statistics<Values>&)
{
    auto result = child(query.get_arg());
    const auto constant = query.get_constant();
    result.constants.emplace(query.get_column().get_value(), std::vector<std::byte>(constant.begin(), constant.end()));
    return conjunction(std::move(result), column_labels(query.columns()));
}
template<ColumnTypes Values, class GetEstimate>
Estimate estimate_operation(QueryView<Values, QueryRenameTag> query, FactorIdentity, GetEstimate&& child, const Statistics<Values>&)
{
    auto result = child(query.get_arg());
    UnorderedMap<uint_t, uint_t> renamed;
    const auto before = query.get_arg().columns(), after = query.columns();
    for (size_t i = 0; i < before.size(); ++i)
        renamed[before[i].label.get_value()] = after[i].label.get_value();
    for (auto& single : result.factors)
        for (auto& [slot, column, ndv] : single.attributes)
            column = renamed.at(column);
    decltype(result.equalities) equalities;
    for (const auto& [lhs, rhs] : result.equalities)
        equalities.emplace(std::min(renamed.at(lhs), renamed.at(rhs)), std::max(renamed.at(lhs), renamed.at(rhs)));
    decltype(result.constants) constants;
    for (const auto& [column, value] : result.constants)
        constants.emplace(renamed.at(column), value);
    result.equalities = std::move(equalities);
    result.constants = std::move(constants);
    return conjunction(std::move(result), column_labels(after));
}
/// Operators beyond System R follow the textbook estimates of Garcia-Molina, Ullman,
/// and Widom (Database Systems: The Complete Book, 2nd ed., 2008, §16.4), with distinct
/// counts under containment of value sets and capped by the result size.
inline RelationStatistics capped(RelationStatistics stats)
{
    for (auto& [column, count] : stats.distinct)
        count = std::min(count, stats.rows);
    return stats;
}
/// Duplicate elimination after projection: min(T(R)/2, ∏ V(R, a)) over the kept columns.
/// A projection that keeps every column only reorders it.
template<ColumnTypes Values, class GetEstimate>
Estimate estimate_operation(QueryView<Values, QueryProjectTag> query, FactorIdentity identity, GetEstimate&& child, const Statistics<Values>&)
{
    const auto argument = child(query.get_arg());
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
template<ColumnTypes Values, class GetEstimate>
Estimate estimate_operation(QueryView<Values, QueryUnionTag> query, FactorIdentity identity, GetEstimate&& child, const Statistics<Values>&)
{
    const auto lhs = child(query.get_lhs()), rhs = child(query.get_rhs());
    RelationStatistics result { bounded(std::max(lhs.rows, rhs.rows) + std::min(lhs.rows, rhs.rows) / 2), {} };
    for (const auto& [column, count] : lhs.distinct)
        result.distinct[column] = std::max(count, rhs.distinct.at(column));
    return factor(capped(std::move(result)), query.columns(), identity);
}
/// Difference: T(R) − T(S)/2; V = V(R, a).
template<ColumnTypes Values, class GetEstimate>
Estimate estimate_operation(QueryView<Values, QueryDifferenceTag> query, FactorIdentity identity, GetEstimate&& child, const Statistics<Values>&)
{
    const auto lhs = child(query.get_lhs()), rhs = child(query.get_rhs());
    return factor(capped({ std::max(0.0, lhs.rows - rhs.rows / 2), lhs.distinct }), query.columns(), identity);
}
/// The literature has no estimate for distances; they are only known when observed.
template<ColumnTypes Values, class GetEstimate>
Estimate estimate_operation(QueryView<Values, QueryDistanceTag>, FactorIdentity, GetEstimate&&, const Statistics<Values>&)
{
    throw std::logic_error("Optimizer: distances have no estimate unless observed.");
}

/// The System R estimate of a query from its children's estimates. An observation of
/// the equivalent caller query replaces the derivation (LEO, VLDB 2001).
template<ColumnTypes Values, std::invocable<QueryView<Values>> GetEstimate>
Estimate estimate(QueryView<Values> query,
                  std::type_identity_t<std::optional<Index<Query<Values>>>> source,
                  GetEstimate&& child,
                  const Statistics<Values>& statistics)
{
    const FactorIdentity identity = QueryFactorIdentity { query.get_index().get_value() };
    if (source)
        if (const auto it = statistics.expressions.find(*source); it != statistics.expressions.end())
            return factor(with_default_distinct(it->second, query.columns(), statistics), query.columns(), identity);
    return ygg::visit([&]<typename Child>(Child operation) { return estimate_operation(operation, identity, child, statistics); }, query.get_variant());
}
}  // namespace ygg::database::optimization_detail

#endif
