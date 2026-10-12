/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SYNTAX_OPERATOR_BUILDER_HPP_
#define YGG_DATABASE_SYNTAX_OPERATOR_BUILDER_HPP_

#include "yggdrasil/database/syntax/query.hpp"

#include <algorithm>
#include <concepts>
#include <span>
#include <vector>

namespace ygg::database
{

/// Constructs queries in a repository: checkout, fill, insert_query.
template<ColumnTypes Values>
class OperatorBuilder
{
    QueryRepository<Values>* m_repository;
    QueryBuilder<Values>* m_builder;

public:
    OperatorBuilder(QueryRepository<Values>& repository, QueryBuilder<Values>& builder) : m_repository(&repository), m_builder(&builder) {}

    QueryRepository<Values>& repository() noexcept { return *m_repository; }
    QueryBuilder<Values>& builder() noexcept { return *m_builder; }

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
    /// The source's operation over schema-identical children of this repository.
    QueryView<Values> clone(QueryView<Values> source, std::span<const QueryView<Values>> children)
    {
        return detail::clone_query(source, children, *m_repository, *m_builder);
    }
    /// Keeps the query's columns with the given labels, in that order.
    QueryView<Values> project(QueryView<Values> query, std::span<const Index<Column>> labels)
    {
        if (std::ranges::equal(column_labels(query.columns()), labels))
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

}  // namespace ygg::database

#endif
