/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_OPTIMIZATION_DETAILS_NORMALIZATION_HPP_
#define YGG_DATABASE_OPTIMIZATION_DETAILS_NORMALIZATION_HPP_

#include "yggdrasil/database/optimization/details/join_block.hpp"
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
/// selections down through join, projection, rename, union, and difference (and into the
/// lhs of semijoins and antijoins); push projections through rename and union, and into the
/// lhs of semijoins and antijoins (keeping the labels shared with the rhs), never through
/// difference (selections already go below projections); collapse nested
/// projections; propagate empty relations. One deterministic bottom-up pass.
namespace ygg::database::detail
{
class Normalizer
{
    template<class Tag>
    using Operation = Data<Query<Tag>>;

    OperatorBuilder m_build;
    std::vector<std::optional<QueryView<>>> m_normalized;

    QueryView<> join(QueryView<> lhs, QueryView<> rhs)
    {
        const auto result = m_build.join(lhs, rhs);
        return is<QueryEmptyTag>(lhs) || is<QueryEmptyTag>(rhs) ? m_build.empty(result.columns()) : result;
    }
    QueryView<> union_(QueryView<> lhs, QueryView<> rhs)
    {
        if (is<QueryEmptyTag>(lhs))
            return rhs;
        if (is<QueryEmptyTag>(rhs))
            return lhs;
        return m_build.template binary<QueryUnionTag>(lhs, rhs);
    }
    QueryView<> difference(QueryView<> lhs, QueryView<> rhs)
    {
        if (is<QueryEmptyTag>(lhs) || is<QueryEmptyTag>(rhs))
            return lhs;
        return m_build.template binary<QueryDifferenceTag>(lhs, rhs);
    }
    /// A semijoin (`Tag` = QuerySemijoinTag) or antijoin: an empty rhs keeps nothing or everything.
    template<class Tag>
    QueryView<> filter_join(QueryView<> lhs, QueryView<> rhs)
    {
        if (is<QueryEmptyTag>(lhs))
            return lhs;
        if (is<QueryEmptyTag>(rhs))
            return std::same_as<Tag, QuerySemijoinTag> ? m_build.empty(lhs.columns()) : lhs;
        return m_build.template binary<Tag>(lhs, rhs);
    }
    /// Labels of the renamed query's argument for the given renamed labels.
    static std::vector<Index<Column>> inverse(QueryView<QueryRenameTag> rename, std::span<const Index<Column>> renamed)
    {
        const auto before = rename.get_arg().columns(), after = rename.columns();
        std::vector<Index<Column>> result;
        for (const auto label : renamed)
            result.push_back(before[column_index(after, label)].label);
        return result;
    }
    QueryView<> rename(QueryView<> arg, std::span<const Index<Column>> columns)
    {
        if (std::ranges::equal(column_labels(arg.columns()), columns))
            return arg;
        const auto result = m_build.template relabel<QueryRenameTag>(arg, columns);
        return is<QueryEmptyTag>(arg) ? m_build.empty(result.columns()) : result;
    }
    QueryView<> project(QueryView<> arg, std::span<const Index<Column>> columns);
    QueryView<> finish_project(QueryView<> arg, std::span<const Index<Column>> columns)
    {
        const auto result = m_build.template relabel<QueryProjectTag>(arg, columns);
        return is<QueryEmptyTag>(arg) ? m_build.empty(result.columns()) : result;
    }

    /// The labels of `arg` in `columns` or in `extra`, in the order of `arg`.
    static std::vector<Index<Column>> kept_labels(QueryView<> arg, std::span<const Index<Column>> columns, std::span<const Index<Column>> extra)
    {
        std::vector<Index<Column>> labels;
        for (const auto& column : arg.columns())
            if (std::ranges::find(columns, column.label) != columns.end() || std::ranges::find(extra, column.label) != extra.end())
                labels.push_back(column.label);
        return labels;
    }
    template<class Tag>
    QueryView<> project_filter_join(QueryView<> arg, std::span<const Index<Column>> columns)
    {
        const auto operation = as<Tag>(arg);
        const auto lhs = operation.get_lhs();
        const auto kept = kept_labels(lhs, columns, column_labels(operation.get_rhs().columns()));
        // Nothing to drop below: the projection stays above.
        if (kept.size() == lhs.columns().size())
            return finish_project(arg, columns);
        return project(filter_join<Tag>(project(lhs, kept), operation.get_rhs()), columns);
    }

    static std::vector<Index<Column>> tested(const Operation<QuerySelectEqualTag>& data) { return { data.lhs_column, data.rhs_column }; }
    static std::vector<Index<Column>> tested(const Operation<QuerySelectValueTag>& data) { return { data.column }; }
    static void retarget(Operation<QuerySelectEqualTag>& data, QueryView<QueryRenameTag> rename)
    {
        const std::array renamed { data.lhs_column, data.rhs_column };
        const auto inner = inverse(rename, renamed);
        data.lhs_column = inner[0];
        data.rhs_column = inner[1];
    }
    static void retarget(Operation<QuerySelectValueTag>& data, QueryView<QueryRenameTag> rename)
    {
        const std::array renamed { data.column };
        data.column = inverse(rename, renamed)[0];
    }
    template<class Tag>
    QueryView<> select(const Operation<Tag>& data, QueryView<> arg)
    {
        const auto columns = tested(data);
        const auto within = [&](QueryView<> query)
        { return std::ranges::all_of(columns, [&](Index<Column> label) { return contains_column(query.columns(), label); }); };
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
        // The output of a semijoin or antijoin is its lhs.
        if (is<QuerySemijoinTag>(arg))
        {
            const auto operation = as<QuerySemijoinTag>(arg);
            return filter_join<QuerySemijoinTag>(select(data, operation.get_lhs()), operation.get_rhs());
        }
        if (is<QueryAntijoinTag>(arg))
        {
            const auto operation = as<QueryAntijoinTag>(arg);
            return filter_join<QueryAntijoinTag>(select(data, operation.get_lhs()), operation.get_rhs());
        }
        return m_build.template make<Tag>(
            [&](auto& filtered)
            {
                filtered = data;
                filtered.arg = arg.get_index();
            });
    }

    /// Rebuilds a query whose children are already normalized.
    template<class Tag>
    QueryView<> normalize(QueryView<> source, QueryView<Tag> query)
    {
        const auto child = [&](QueryView<> operand) { return (*this)(operand); };
        if constexpr (std::same_as<Tag, QueryJoinTag>)
            return join(child(query.get_lhs()), child(query.get_rhs()));
        else if constexpr (std::same_as<Tag, QueryUnionTag>)
            return union_(child(query.get_lhs()), child(query.get_rhs()));
        else if constexpr (std::same_as<Tag, QueryDifferenceTag>)
            return difference(child(query.get_lhs()), child(query.get_rhs()));
        else if constexpr (std::same_as<Tag, QuerySemijoinTag> || std::same_as<Tag, QueryAntijoinTag>)
            return filter_join<Tag>(child(query.get_lhs()), child(query.get_rhs()));
        else if constexpr (std::same_as<Tag, QueryProjectTag>)
            return project(child(query.get_arg()), query.get_labels());
        else if constexpr (std::same_as<Tag, QueryRenameTag>)
            return rename(child(query.get_arg()), query.get_labels());
        else if constexpr (std::same_as<Tag, QuerySelectEqualTag> || std::same_as<Tag, QuerySelectValueTag>)
            return select(query.get_data(), child(query.get_arg()));
        else
        {
            std::vector<QueryView<>> children;
            bool absent = false;
            for_each_child(query,
                           [&](QueryView<> operand)
                           {
                               children.push_back(child(operand));
                               absent |= is<QueryEmptyTag>(children.back());
                           });
            const auto result = m_build.clone(source, children);
            if constexpr (std::same_as<Tag, QueryGenericJoinTag>)
                if (absent)
                    return m_build.empty(result.columns());
            return result;
        }
    }

public:
    Normalizer(QueryRepository& repository, QueryBuilder& builder, size_t source_size) :
        m_build(repository, builder),
        m_normalized(source_size)
    {
    }

    /// The normalized equivalent of a query of the source repository.
    QueryView<> operator()(QueryView<> source)
    {
        auto& normalized = m_normalized.at(source.get_index().get_value());
        if (!normalized)
            normalized = ygg::visit([&](auto query) { return normalize(source, query); }, source.get_variant());
        return *normalized;
    }
};
}  // namespace ygg::database::detail

#endif
