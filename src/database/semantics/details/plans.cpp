/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/plans.hpp"
#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>

namespace ygg::database::detail
{
void append_column(urd::Store<urd::sequence<ColumnLayout>>& output, ColumnLayout column)
{
    column.offset = output.empty() ? 0 : output.back().offset + output.back().size;
    if (column.size > std::numeric_limits<size_t>::max() - column.offset)
        throw std::overflow_error("Relational operation: row size overflow.");
    output.push_back(column);
}
}

namespace ygg::database
{
void ProjectionPlan::compile(std::span<const urd::index<Column>> columns)
{
    auto output = urd::Store<urd::sequence<ColumnLayout>>();
    detail::projection_positions(m_input.span(), columns, output, m_positions);
    m_output.assign(output);
}
}

namespace ygg::database
{
ProjectionPlan::ProjectionPlan(std::span<const ColumnLayout> input, std::span<const urd::index<Column>> columns) : m_input(input) { compile(columns); }
}

namespace ygg::database
{
ProjectionPlan::ProjectionPlan(std::span<const ColumnLayout> input, std::initializer_list<urd::index<Column>> columns) :
    ProjectionPlan(input, std::span<const urd::index<Column>>(columns))
{
}
}

namespace ygg::database
{
void JoinPlan::compile()
{
    auto columns = urd::Store<urd::sequence<ColumnLayout>>();
    detail::join_positions(m_lhs.span(), m_rhs.span(), columns, m_lhs_keys, m_rhs_keys, m_rhs_payload);
    m_output.assign(columns);
}
}

namespace ygg::database
{
JoinPlan::JoinPlan(std::span<const ColumnLayout> lhs, std::span<const ColumnLayout> rhs) : m_lhs(lhs), m_rhs(rhs) { compile(); }
}

namespace ygg::database
{
ProjectionPlan::ProjectionPlan(std::span<const urd::index<Column>> input, std::span<const urd::index<Column>> columns) : m_input(input) { compile(columns); }
}

namespace ygg::database
{
ProjectionPlan::ProjectionPlan(std::initializer_list<urd::index<Column>> input, std::initializer_list<urd::index<Column>> columns) :
    ProjectionPlan(std::span<const urd::index<Column>>(input), std::span<const urd::index<Column>>(columns))
{
}
}

namespace ygg::database
{
JoinPlan::JoinPlan(std::span<const urd::index<Column>> lhs, std::span<const urd::index<Column>> rhs) : m_lhs(lhs), m_rhs(rhs) { compile(); }
}

namespace ygg::database
{
JoinPlan::JoinPlan(std::initializer_list<urd::index<Column>> lhs, std::initializer_list<urd::index<Column>> rhs) :
    JoinPlan(std::span<const urd::index<Column>>(lhs), std::span<const urd::index<Column>>(rhs))
{
}
}
