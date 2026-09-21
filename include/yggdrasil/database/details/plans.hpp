/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_DETAILS_PLANS_HPP_
#define YGG_DATABASE_DETAILS_PLANS_HPP_

#include "yggdrasil/database/plans.hpp"

#include <algorithm>
#include <vector>

namespace ygg::database
{

namespace detail
{
template<typename Positions>
void projection_positions(ColumnsView input, ColumnsView columns, Positions& positions)
{
    positions.clear();
    positions.reserve(columns.size());
    for (const auto column : columns)
        positions.push_back(input.column_index(column));
}

template<typename Positions>
void join_positions(ColumnsView lhs, ColumnsView rhs, std::vector<Column>& columns, Positions& lhs_keys, Positions& rhs_keys, Positions& rhs_payload)
{
    columns.assign(lhs.begin(), lhs.end());
    lhs_keys.clear();
    rhs_keys.clear();
    rhs_payload.clear();
    for (size_t j = 0; j < rhs.size(); ++j)
    {
        const auto it = std::ranges::find(lhs, rhs[j]);
        if (it == lhs.end())
        {
            columns.push_back(rhs[j]);
            rhs_payload.push_back(j);
        }
        else
        {
            lhs_keys.push_back(static_cast<size_t>(it - lhs.begin()));
            rhs_keys.push_back(j);
        }
    }
}
}  // namespace detail

inline ProjectionPlan::ProjectionPlan(ColumnsView input, ColumnsView columns) : m_input(input), m_output(columns)
{
    detail::projection_positions(input, columns, m_positions);
}

inline ProjectionPlan::ProjectionPlan(std::span<const Column> input, std::span<const Column> columns) : ProjectionPlan(ColumnsView(input), ColumnsView(columns))
{
}

inline ProjectionPlan::ProjectionPlan(ColumnsView input, std::initializer_list<Column> columns) :
    ProjectionPlan(input, ColumnsView(std::span<const Column>(columns)))
{
}

inline ProjectionPlan::ProjectionPlan(std::span<const Column> input, std::initializer_list<Column> columns) :
    ProjectionPlan(input, std::span<const Column>(columns))
{
}

inline ProjectionPlan::ProjectionPlan(std::initializer_list<Column> input, std::initializer_list<Column> columns) :
    ProjectionPlan(std::span<const Column>(input), std::span<const Column>(columns))
{
}

inline JoinPlan::JoinPlan(ColumnsView lhs, ColumnsView rhs) : m_lhs(lhs), m_rhs(rhs)
{
    auto columns = std::vector<Column>();
    detail::join_positions(lhs, rhs, columns, m_lhs_keys, m_rhs_keys, m_rhs_payload);
    m_output = Columns(columns);
}

inline JoinPlan::JoinPlan(std::span<const Column> lhs, std::span<const Column> rhs) : JoinPlan(ColumnsView(lhs), ColumnsView(rhs)) {}

inline JoinPlan::JoinPlan(std::initializer_list<Column> lhs, std::initializer_list<Column> rhs) :
    JoinPlan(std::span<const Column>(lhs), std::span<const Column>(rhs))
{
}

}  // namespace ygg::database

#endif
