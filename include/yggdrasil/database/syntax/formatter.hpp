/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_SYNTAX_FORMATTER_HPP_
#define YGG_DATABASE_SYNTAX_FORMATTER_HPP_

#include "yggdrasil/database/syntax/query.hpp"
#include "yggdrasil/formatting/formatter.hpp"

#include <iterator>

namespace ygg::database
{
template<ColumnTypes Values, typename Tag>
constexpr const char* query_operation_name(QueryView<Values, Tag>)
{
    using Concrete = QueryView<Values, Tag>;
    if constexpr (std::same_as<Concrete, QueryView<Values, QueryInputTag>>)
        return "input";
    else if constexpr (std::same_as<Concrete, QueryView<Values, QueryEmptyTag>>)
        return "empty";
    else if constexpr (std::same_as<Concrete, QueryView<Values, QueryJoinTag>>)
        return "hash_join";
    else if constexpr (std::same_as<Concrete, QueryView<Values, QueryProjectTag>>)
        return "project";
    else if constexpr (std::same_as<Concrete, QueryView<Values, QueryRenameTag>>)
        return "rename";
    else if constexpr (std::same_as<Concrete, QueryView<Values, QuerySelectEqualTag>>)
        return "select_equal";
    else if constexpr (std::same_as<Concrete, QueryView<Values, QuerySelectValueTag>>)
        return "select_value";
    else if constexpr (std::same_as<Concrete, QueryView<Values, QueryUnionTag>>)
        return "union";
    else if constexpr (std::same_as<Concrete, QueryView<Values, QueryDifferenceTag>>)
        return "difference";
    else if constexpr (std::same_as<Concrete, QueryView<Values, QueryDistanceTag>>)
        return "distance";
    else
    {
        static_assert(std::same_as<Concrete, QueryView<Values, QueryGenericJoinTag>>);
        return "generic_join";
    }
}

namespace detail
{
template<typename OutputIt, ColumnTypes Values>
OutputIt format_query_plan(OutputIt out, const QueryPlan<Values>& plan)
{
    for (size_t id = 0; id < plan.node_count(); ++id)
        ygg::visit(
            [&]<typename Concrete>(Concrete operation)
            {
                out = fmt::format_to(out, "{}: {}(", id, query_operation_name(operation));
                bool first = true;
                for_each_child(operation,
                               [&](QueryView<Values> child)
                               {
                                   out = fmt::format_to(out, "{}{}", first ? "" : ",", child.get_index().get_value());
                                   first = false;
                               });
                out = fmt::format_to(out, ") columns=[");
                const auto columns = operation.columns();
                for (size_t i = 0; i < columns.size(); ++i)
                    out = fmt::format_to(out, "{}{}:{}", i ? "," : "", columns[i].label.get_value(), columns[i].type);
                out = fmt::format_to(out, "]");
                if constexpr (std::same_as<Concrete, QueryView<Values, QueryInputTag>>)
                    out = fmt::format_to(out, " slot={}", operation.get_input_slot());
                if constexpr (std::same_as<Concrete, QueryView<Values, QueryGenericJoinTag>>)
                {
                    out = fmt::format_to(out, " variables=[");
                    const auto variables = operation.get_variable_order();
                    for (size_t i = 0; i < variables.size(); ++i)
                        out = fmt::format_to(out, "{}{}", i ? "," : "", variables[i].get_value());
                    out = fmt::format_to(out, "]");
                }
                out = fmt::format_to(out, "\n");
            },
            plan[Index<Query<Values>>(to_uint_t(id))].get_variant());
    out = fmt::format_to(out, "roots=[");
    const auto roots = plan.roots();
    for (size_t i = 0; i < roots.size(); ++i)
        out = fmt::format_to(out, "{}{}", i ? "," : "", roots[i].get_value());
    return fmt::format_to(out, "]\n");
}
}  // namespace detail

template<ColumnTypes Values>
std::string explain(const QueryPlan<Values>& plan)
{
    fmt::memory_buffer out;
    detail::format_query_plan(std::back_inserter(out), plan);
    return fmt::to_string(out);
}
}  // namespace ygg::database

#if YGG_ENABLE_FMT_FORMATTERS
namespace fmt
{
template<ygg::database::ColumnTypes Values>
struct formatter<ygg::database::QueryPlan<Values>, char>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const ygg::database::QueryPlan<Values>& plan, FormatContext& ctx) const
    {
        return ygg::database::detail::format_query_plan(ctx.out(), plan);
    }
};
}  // namespace fmt
#endif

#endif
