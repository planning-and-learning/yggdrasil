/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/query_evaluation.hpp"

#include <variant>

namespace ygg::database::detail
{
bool query_accepts(QueryView<QuerySelectEqualTag> selection, std::span<const std::byte> row)
{
    const auto columns = selection.columns();
    return std::ranges::equal(column_bytes(row, columns[selection.get_lhs_position()]), column_bytes(row, columns[selection.get_rhs_position()]));
}
}

namespace ygg::database::detail
{
bool query_accepts(QueryView<QuerySelectValueTag> selection, std::span<const std::byte> row)
{ return std::ranges::equal(column_bytes(row, selection.columns()[selection.get_position()]), selection.get_constant()); }
}

namespace ygg::database
{
size_t QueryEvaluator::memory_usage() const noexcept
{
    // get_if rather than std::visit, which throws for a valueless variant; states are never valueless.
    const auto usage = []<typename Prepared>(const Prepared* prepared) noexcept -> size_t
    {
        if constexpr (std::same_as<Prepared, std::monostate>)
            return 0;
        else
            return prepared ? prepared->memory_usage() : 0;
    };
    const auto state_usage = [&]<typename... States>(const std::variant<States...>& state) noexcept { return (usage(std::get_if<States>(&state)) + ...); };
    size_t result = m_workspace.memory_usage();
    for (const auto& node : m_nodes)
        result += node.result.memory_usage() + state_usage(node.state);
    return result;
}
}
