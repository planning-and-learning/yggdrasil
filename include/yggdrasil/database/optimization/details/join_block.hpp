/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_OPTIMIZATION_DETAILS_JOIN_BLOCK_HPP_
#define YGG_DATABASE_OPTIMIZATION_DETAILS_JOIN_BLOCK_HPP_

#include "yggdrasil/database/syntax/query.hpp"

#include <algorithm>
#include <concepts>
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

template<class Tag, ColumnTypes Values>
bool is(QueryView<Values> query)
{
    return query.get_variant().template is<Index<Query<Values, Tag>>>();
}
template<class Tag, ColumnTypes Values>
QueryView<Values, Tag> as(QueryView<Values> query)
{
    return query.get_variant().template get<Index<Query<Values, Tag>>>();
}

/// Constructs the optimizer's queries in a repository: checkout, fill, insert_query.
template<ColumnTypes Values>
class OperatorBuilder
{
    QueryRepository<Values>* m_repository;
    QueryBuilder<Values>* m_builder;

public:
    OperatorBuilder(QueryRepository<Values>& repository, QueryBuilder<Values>& builder) : m_repository(&repository), m_builder(&builder) {}

    template<class Tag, std::invocable<Data<Query<Values, Tag>>&> Fill>
    QueryView<Values> make(Fill&& fill)
    {
        auto data = checkout<Query<Values, Tag>>(*m_builder);
        fill(*data);
        return insert_query(*m_repository, *m_builder, *data);
    }
    QueryView<Values> empty(std::span<const ColumnLayout> columns)
    {
        return make<QueryEmptyTag>([&](auto& data) { data.columns.set(columns.begin(), columns.end()); });
    }
    template<class Tag>
    QueryView<Values> binary(QueryView<Values> lhs, QueryView<Values> rhs)
    {
        return make<Tag>(
            [&](auto& data)
            {
                data.lhs = lhs.get_index();
                data.rhs = rhs.get_index();
            });
    }
    template<class Tag>
    QueryView<Values> relabel(QueryView<Values> arg, std::span<const Index<Column>> labels)
    {
        return make<Tag>(
            [&](auto& data)
            {
                data.arg = arg.get_index();
                data.labels.set(labels.begin(), labels.end());
            });
    }
    QueryView<Values> join(QueryView<Values> lhs, QueryView<Values> rhs) { return binary<QueryJoinTag>(lhs, rhs); }
    /// Keeps the query's columns with the given labels, in that order.
    QueryView<Values> project(QueryView<Values> query, std::span<const Index<Column>> labels)
    {
        if (std::ranges::equal(detail::query_labels(query.columns()), labels))
            return query;
        return relabel<QueryProjectTag>(query, labels);
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
        return make<QueryGenericJoinTag>(
            [&](auto& data)
            {
                for (const auto atom : atoms)
                    data.inputs.push_back(atom.get_index());
                data.variable_order.set(order.begin(), order.end());
                data.output_order.set(order.begin(), order.end());
            });
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
