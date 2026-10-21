/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/query_evaluation.hpp"

namespace ygg::database
{
namespace detail
{
bool query_accepts(QueryView<QuerySelectEqualTag> selection, std::span<const std::byte> row)
{
    const auto columns = selection.columns();
    return std::ranges::equal(column_bytes(row, columns[selection.get_lhs_position()]), column_bytes(row, columns[selection.get_rhs_position()]));
}
}
}

namespace ygg::database
{
namespace detail
{
bool query_accepts(QueryView<QuerySelectValueTag> selection, std::span<const std::byte> row)
{
    return std::ranges::equal(column_bytes(row, selection.columns()[selection.get_position()]), selection.get_constant());
}
}
}

namespace ygg::database
{
size_t QueryEvaluator::memory_usage() const noexcept
{
    size_t result = m_workspace.memory_usage();
    for (const auto& node : m_nodes)
    {
        result += node.result.memory_usage();
        std::visit(
            [&]<typename Prepared>(const Prepared& state)
            {
                if constexpr (!std::same_as<Prepared, std::monostate>)
                    result += state.memory_usage();
            },
            node.state);
    }
    return result;
}
}
