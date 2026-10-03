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
void projection_positions(std::span<const Index<Column>> input, std::span<const Index<Column>> columns, Positions& positions)
{
    positions.clear();
    positions.reserve(columns.size());
    for (const auto column : columns)
        positions.push_back(column_index(input, column));
}

template<typename Positions>
void join_positions(std::span<const Index<Column>> lhs,
                    std::span<const Index<Column>> rhs,
                    std::vector<Index<Column>>& columns,
                    Positions& lhs_keys,
                    Positions& rhs_keys,
                    Positions& rhs_payload)
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

inline ProjectionPlan::ProjectionPlan(std::span<const Index<Column>> input, std::span<const Index<Column>> columns) : m_input(input), m_output(columns)
{
    detail::projection_positions(m_input.span(), m_output.span(), m_positions);
}

inline ProjectionPlan::ProjectionPlan(std::span<const Index<Column>> input, std::initializer_list<Index<Column>> columns) :
    ProjectionPlan(input, std::span<const Index<Column>>(columns))
{
}

inline ProjectionPlan::ProjectionPlan(std::initializer_list<Index<Column>> input, std::initializer_list<Index<Column>> columns) :
    ProjectionPlan(std::span<const Index<Column>>(input), std::span<const Index<Column>>(columns))
{
}

inline JoinPlan::JoinPlan(std::span<const Index<Column>> lhs, std::span<const Index<Column>> rhs) : m_lhs(lhs), m_rhs(rhs)
{
    auto columns = std::vector<Index<Column>>();
    detail::join_positions(m_lhs.span(), m_rhs.span(), columns, m_lhs_keys, m_rhs_keys, m_rhs_payload);
    m_output = Builder<Columns>(columns);
}

inline JoinPlan::JoinPlan(std::initializer_list<Index<Column>> lhs, std::initializer_list<Index<Column>> rhs) :
    JoinPlan(std::span<const Index<Column>>(lhs), std::span<const Index<Column>>(rhs))
{
}

}  // namespace ygg::database

#endif
