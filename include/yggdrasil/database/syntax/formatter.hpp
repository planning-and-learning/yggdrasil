/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_SYNTAX_FORMATTER_HPP_
#define YGG_DATABASE_SYNTAX_FORMATTER_HPP_

#include "yggdrasil/database/syntax/query.hpp"
#include "yggdrasil/formatting/formatter.hpp"

#include <fmt/ranges.h>
#include <iterator>
#include <ranges>
#include <vector>

namespace ygg::database
{
template<typename Tag>
constexpr const char* query_operation_name(QueryView<Tag>)
{
    using Concrete = QueryView<Tag>;
    if constexpr (std::same_as<Concrete, QueryView<QueryInputTag>>)
        return "input";
    else if constexpr (std::same_as<Concrete, QueryView<QueryEmptyTag>>)
        return "empty";
    else if constexpr (std::same_as<Concrete, QueryView<QueryJoinTag>>)
        return "hash_join";
    else if constexpr (std::same_as<Concrete, QueryView<QueryProjectTag>>)
        return "project";
    else if constexpr (std::same_as<Concrete, QueryView<QueryRenameTag>>)
        return "rename";
    else if constexpr (std::same_as<Concrete, QueryView<QuerySelectEqualTag>>)
        return "select_equal";
    else if constexpr (std::same_as<Concrete, QueryView<QuerySelectValueTag>>)
        return "select_value";
    else if constexpr (std::same_as<Concrete, QueryView<QueryUnionTag>>)
        return "union";
    else if constexpr (std::same_as<Concrete, QueryView<QueryDifferenceTag>>)
        return "difference";
    else if constexpr (std::same_as<Concrete, QueryView<QueryDistanceTag>>)
        return "distance";
    else if constexpr (std::same_as<Concrete, QueryView<QuerySemijoinTag>>)
        return "semijoin";
    else if constexpr (std::same_as<Concrete, QueryView<QueryAntijoinTag>>)
        return "antijoin";
    else
    {
        static_assert(std::same_as<Concrete, QueryView<QueryGenericJoinTag>>);
        return "generic_join";
    }
}

namespace detail
{
template<typename OutputIt>
OutputIt format_query_plan(OutputIt out, const QueryPlan& plan)
{
    // Plain values, so explanations do not depend on the optional public formatters.
    const auto value = [](auto index) { return index.get_value(); };
    for (size_t id = 0; id < plan.node_count(); ++id)
        ygg::visit(
            [&]<typename Concrete>(Concrete operation)
            {
                std::vector<uint_t> children;
                for_each_child(operation, [&](QueryView<> child) { children.push_back(child.get_index().get_value()); });
                const auto columns = operation.columns()
                                     | std::views::transform([](const ColumnLayout& column) { return fmt::format("{}:{}", column.label.get_value(), column.type); });
                out = fmt::format_to(out, "{}: {}({}) columns=[{}]", id, query_operation_name(operation), fmt::join(children, ","), fmt::join(columns, ","));
                if constexpr (std::same_as<Concrete, QueryView<QueryInputTag>>)
                    out = fmt::format_to(out, " slot={}", operation.get_input_slot());
                if constexpr (std::same_as<Concrete, QueryView<QueryGenericJoinTag>>)
                    out = fmt::format_to(out, " variables=[{}]", fmt::join(operation.get_variable_order() | std::views::transform(value), ","));
                out = fmt::format_to(out, "\n");
            },
            plan[Index<Query<>>(to_uint_t(id))].get_variant());
    return fmt::format_to(out, "roots=[{}]\n", fmt::join(plan.roots() | std::views::transform(value), ","));
}
}  // namespace detail

std::string explain(const QueryPlan& plan);
}  // namespace ygg::database

#if YGG_ENABLE_FMT_FORMATTERS
namespace fmt
{
template<>
struct formatter<ygg::database::QueryPlan, char>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const ygg::database::QueryPlan& plan, FormatContext& ctx) const
    {
        return ygg::database::detail::format_query_plan(ctx.out(), plan);
    }
};
}  // namespace fmt
#endif

#endif
