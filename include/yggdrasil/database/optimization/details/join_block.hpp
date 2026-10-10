/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_OPTIMIZATION_DETAILS_JOIN_BLOCK_HPP_
#define YGG_DATABASE_OPTIMIZATION_DETAILS_JOIN_BLOCK_HPP_

#include "yggdrasil/database/syntax/query.hpp"

#include <algorithm>
#include <span>
#include <vector>

namespace ygg::database::optimization_detail
{
/// A natural join of atoms projected to an ordered output: π_output(atom_1 ⋈ … ⋈ atom_n).
/// Atoms are planned operands; columns with equal labels are join variables.
template<ColumnTypes Values>
struct JoinBlock
{
    std::vector<QueryView<Values>> atoms;
    std::vector<ColumnLayout> output;
};

template<ColumnTypes Values>
std::vector<Index<Column>> variables(QueryView<Values> query)
{
    auto result = detail::query_labels(query.columns());
    std::ranges::sort(result);
    return result;
}

inline bool shares(std::span<const Index<Column>> lhs, std::span<const Index<Column>> rhs)
{
    return std::ranges::any_of(lhs, [&](Index<Column> label) { return std::ranges::binary_search(rhs, label); });
}

/// Builds the planned operators of a block in the plan repository.
template<ColumnTypes Values>
class BlockBuilder
{
    QueryRepository<Values>* m_repository;
    QueryBuilder<Values>* m_builder;

public:
    BlockBuilder(QueryRepository<Values>& repository, QueryBuilder<Values>& builder) : m_repository(&repository), m_builder(&builder) {}

    QueryView<Values> join(QueryView<Values> lhs, QueryView<Values> rhs)
    {
        auto data = checkout<Query<Values, QueryJoinTag>>(*m_builder);
        data->lhs = lhs.get_index();
        data->rhs = rhs.get_index();
        return insert_query(*m_repository, *m_builder, *data);
    }
    /// Keeps the query's columns with the given labels, in that order.
    QueryView<Values> project(QueryView<Values> query, std::span<const Index<Column>> labels)
    {
        if (std::ranges::equal(detail::query_labels(query.columns()), labels))
            return query;
        auto data = checkout<Query<Values, QueryProjectTag>>(*m_builder);
        data->arg = query.get_index();
        data->labels.set(labels.begin(), labels.end());
        return insert_query(*m_repository, *m_builder, *data);
    }
    /// Keeps the query's columns whose labels occur in the sorted set, in schema order.
    QueryView<Values> restrict(QueryView<Values> query, std::span<const Index<Column>> keep)
    {
        std::vector<Index<Column>> labels;
        for (const auto& column : query.columns())
            if (std::ranges::binary_search(keep, column.label))
                labels.push_back(column.label);
        return project(query, labels);
    }
    /// R ⋉ S, written R ⋈ π_common(S) with the existing operators.
    QueryView<Values> semijoin(QueryView<Values> lhs, QueryView<Values> rhs)
    {
        return join(lhs, restrict(rhs, variables(lhs)));
    }
    /// Generic Join over the atoms; the variable order is also the output order.
    QueryView<Values> generic_join(std::span<const QueryView<Values>> atoms, std::span<const Index<Column>> order)
    {
        auto data = checkout<Query<Values, QueryGenericJoinTag>>(*m_builder);
        for (const auto atom : atoms)
            data->inputs.push_back(atom.get_index());
        data->variable_order.set(order.begin(), order.end());
        data->output_order.set(order.begin(), order.end());
        return insert_query(*m_repository, *m_builder, *data);
    }
};

/// The labels as a sorted set.
inline std::vector<Index<Column>> sorted(std::vector<Index<Column>> labels)
{
    std::ranges::sort(labels);
    labels.erase(std::unique(labels.begin(), labels.end()), labels.end());
    return labels;
}

/// A variable order with the given variables first (in order), then the rest by label.
template<ColumnTypes Values>
std::vector<Index<Column>> variable_order(std::span<const QueryView<Values>> atoms, std::span<const Index<Column>> first)
{
    std::vector<Index<Column>> rest;
    for (const auto atom : atoms)
        for (const auto label : variables(atom))
            if (std::ranges::find(first, label) == first.end())
                rest.push_back(label);
    std::ranges::sort(rest);
    rest.erase(std::unique(rest.begin(), rest.end()), rest.end());
    std::vector<Index<Column>> result(first.begin(), first.end());
    result.insert(result.end(), rest.begin(), rest.end());
    return result;
}
}  // namespace ygg::database::optimization_detail

#endif
