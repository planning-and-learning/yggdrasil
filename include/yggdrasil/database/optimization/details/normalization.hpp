/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_OPTIMIZATION_DETAILS_NORMALIZATION_HPP_
#define YGG_DATABASE_OPTIMIZATION_DETAILS_NORMALIZATION_HPP_

#include "yggdrasil/database/syntax/query.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <optional>
#include <span>
#include <utility>
#include <vector>

/// Algebraic normalization by the classic heuristics (H. Garcia-Molina, J. D. Ullman,
/// J. Widom, "Database Systems: The Complete Book", 2nd ed., 2008, ch. 16): push
/// selections down through join, projection, rename, union, and difference; push
/// projections through rename and union, never through difference; collapse nested
/// projections; propagate empty relations. One deterministic bottom-up pass.
namespace ygg::database::optimization_detail
{
template<ColumnTypes Values>
class Normalizer
{
    template<class Tag>
    using Operation = Data<Query<Values, Tag>>;

    QueryRepository<Values>* m_repository;
    QueryBuilder<Values>* m_builder;
    std::vector<std::optional<QueryView<Values>>> m_normalized;

    template<class Tag>
    QueryView<Values> insert(Operation<Tag>& data)
    {
        return insert_query(*m_repository, *m_builder, data);
    }
    template<class Tag>
    static bool is(QueryView<Values> query)
    {
        return query.get_variant().template is<Index<Query<Values, Tag>>>();
    }
    template<class Tag>
    static QueryView<Values, Tag> as(QueryView<Values> query)
    {
        return query.get_variant().template get<Index<Query<Values, Tag>>>();
    }
    static std::vector<Index<Column>> labels(std::span<const ColumnLayout> columns) { return detail::query_labels(columns); }

    QueryView<Values> empty(std::span<const ColumnLayout> columns)
    {
        auto data = checkout<Query<Values, QueryEmptyTag>>(*m_builder);
        data->columns.set(columns.begin(), columns.end());
        return insert(*data);
    }
    template<class Tag>
    QueryView<Values> binary(QueryView<Values> lhs, QueryView<Values> rhs)
    {
        auto data = checkout<Query<Values, Tag>>(*m_builder);
        data->lhs = lhs.get_index();
        data->rhs = rhs.get_index();
        return insert(*data);
    }
    QueryView<Values> join(QueryView<Values> lhs, QueryView<Values> rhs)
    {
        const auto result = binary<QueryJoinTag>(lhs, rhs);
        return is<QueryEmptyTag>(lhs) || is<QueryEmptyTag>(rhs) ? empty(result.columns()) : result;
    }
    QueryView<Values> union_(QueryView<Values> lhs, QueryView<Values> rhs)
    {
        if (is<QueryEmptyTag>(lhs))
            return rhs;
        if (is<QueryEmptyTag>(rhs))
            return lhs;
        return binary<QueryUnionTag>(lhs, rhs);
    }
    QueryView<Values> difference(QueryView<Values> lhs, QueryView<Values> rhs)
    {
        if (is<QueryEmptyTag>(lhs) || is<QueryEmptyTag>(rhs))
            return lhs;
        return binary<QueryDifferenceTag>(lhs, rhs);
    }
    template<class Tag>
    QueryView<Values> relabel(QueryView<Values> arg, std::span<const Index<Column>> columns)
    {
        auto data = checkout<Query<Values, Tag>>(*m_builder);
        data->arg = arg.get_index();
        data->labels.set(columns.begin(), columns.end());
        return insert(*data);
    }
    /// Labels of the renamed query's argument for the given renamed labels.
    static std::vector<Index<Column>> inverse(QueryView<Values, QueryRenameTag> rename, std::span<const Index<Column>> renamed)
    {
        const auto before = rename.get_arg().columns(), after = rename.columns();
        std::vector<Index<Column>> result;
        for (const auto label : renamed)
            result.push_back(before[column_index(after, label)].label);
        return result;
    }
    QueryView<Values> rename(QueryView<Values> arg, std::span<const Index<Column>> columns)
    {
        if (std::ranges::equal(labels(arg.columns()), columns))
            return arg;
        const auto result = relabel<QueryRenameTag>(arg, columns);
        return is<QueryEmptyTag>(arg) ? empty(result.columns()) : result;
    }
    QueryView<Values> project(QueryView<Values> arg, std::span<const Index<Column>> columns)
    {
        if (std::ranges::equal(labels(arg.columns()), columns))
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
        const auto result = relabel<QueryProjectTag>(arg, columns);
        return is<QueryEmptyTag>(arg) ? empty(result.columns()) : result;
    }

    static std::vector<Index<Column>> tested(const Operation<QuerySelectEqualTag>& data) { return { data.lhs_column, data.rhs_column }; }
    static std::vector<Index<Column>> tested(const Operation<QuerySelectValueTag>& data) { return { data.column }; }
    static void retarget(Operation<QuerySelectEqualTag>& data, QueryView<Values, QueryRenameTag> rename)
    {
        const std::array renamed { data.lhs_column, data.rhs_column };
        const auto inner = inverse(rename, renamed);
        data.lhs_column = inner[0];
        data.rhs_column = inner[1];
    }
    static void retarget(Operation<QuerySelectValueTag>& data, QueryView<Values, QueryRenameTag> rename)
    {
        const std::array renamed { data.column };
        data.column = inverse(rename, renamed)[0];
    }
    template<class Tag>
    QueryView<Values> select(Operation<Tag> data, QueryView<Values> arg)
    {
        const auto columns = tested(data);
        const auto within = [&](QueryView<Values> query)
        { return std::ranges::all_of(columns, [&](Index<Column> label) { return has_column(query.columns(), label); }); };
        if (is<QueryEmptyTag>(arg))
            return arg;
        if (is<QueryJoinTag>(arg))
        {
            const auto operation = as<QueryJoinTag>(arg);
            if (within(operation.get_lhs()))
                return join(select(data, operation.get_lhs()), operation.get_rhs());
            if (within(operation.get_rhs()))
                return join(operation.get_lhs(), select(data, operation.get_rhs()));
        }
        if (is<QueryProjectTag>(arg))
        {
            const auto operation = as<QueryProjectTag>(arg);
            return project(select(data, operation.get_arg()), operation.get_labels());
        }
        if (is<QueryRenameTag>(arg))
        {
            const auto operation = as<QueryRenameTag>(arg);
            auto inner = data;
            retarget(inner, operation);
            return rename(select(inner, operation.get_arg()), operation.get_labels());
        }
        if (is<QueryUnionTag>(arg))
        {
            const auto operation = as<QueryUnionTag>(arg);
            return union_(select(data, operation.get_lhs()), select(data, operation.get_rhs()));
        }
        if (is<QueryDifferenceTag>(arg))
        {
            const auto operation = as<QueryDifferenceTag>(arg);
            return difference(select(data, operation.get_lhs()), select(data, operation.get_rhs()));
        }
        auto filtered = checkout<Query<Values, Tag>>(*m_builder);
        *filtered = data;
        filtered->arg = arg.get_index();
        return insert(*filtered);
    }
    static bool has_column(std::span<const ColumnLayout> columns, Index<Column> label)
    {
        return std::ranges::any_of(columns, [&](const auto& column) { return column.label == label; });
    }

    /// Rebuilds a query whose children are already normalized.
    template<class Tag>
    QueryView<Values> normalize(QueryView<Values> source, QueryView<Values, Tag> query)
    {
        const auto child = [&](QueryView<Values> source) { return (*this)(source); };
        if constexpr (std::same_as<Tag, QueryJoinTag>)
            return join(child(query.get_lhs()), child(query.get_rhs()));
        else if constexpr (std::same_as<Tag, QueryUnionTag>)
            return union_(child(query.get_lhs()), child(query.get_rhs()));
        else if constexpr (std::same_as<Tag, QueryDifferenceTag>)
            return difference(child(query.get_lhs()), child(query.get_rhs()));
        else if constexpr (std::same_as<Tag, QueryProjectTag>)
            return project(child(query.get_arg()), query.get_labels());
        else if constexpr (std::same_as<Tag, QueryRenameTag>)
            return rename(child(query.get_arg()), query.get_labels());
        else if constexpr (std::same_as<Tag, QuerySelectEqualTag> || std::same_as<Tag, QuerySelectValueTag>)
            return select(query.get_data(), child(query.get_arg()));
        else
        {
            std::vector<QueryView<Values>> children;
            bool absent = false;
            for_each_child(query,
                           [&](QueryView<Values> source)
                           {
                               children.push_back(child(source));
                               absent |= is<QueryEmptyTag>(children.back());
                           });
            const auto result = detail::clone_query(source, std::span<const QueryView<Values>>(children), *m_repository, *m_builder);
            if constexpr (std::same_as<Tag, QueryGenericJoinTag>)
                if (absent)
                    return empty(result.columns());
            return result;
        }
    }

public:
    Normalizer(QueryRepository<Values>& repository, QueryBuilder<Values>& builder, size_t source_size) :
        m_repository(&repository),
        m_builder(&builder),
        m_normalized(source_size)
    {
    }

    /// The normalized equivalent of a query of the source repository.
    QueryView<Values> operator()(QueryView<Values> source)
    {
        auto& normalized = m_normalized.at(source.get_index().get_value());
        if (!normalized)
            normalized = ygg::visit([&](auto query) { return normalize(source, query); }, source.get_variant());
        return *normalized;
    }
};
}  // namespace ygg::database::optimization_detail

#endif
