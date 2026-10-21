/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/generic_join.hpp"

namespace ygg::database
{
namespace detail
{
void count_generic_join_work([[maybe_unused]] size_t GenericJoinWork::*member) noexcept
{
#ifdef YGG_GENERIC_JOIN_INSTRUMENTATION
    ++(generic_join_work.*member);
#endif
}
}
}

namespace ygg::database
{
namespace detail
{
bool GenericJoinTrie::contains(std::span<const std::byte> row, std::span<const ColumnSlice> keys) const
{
    const auto* node = this;
    for (const auto key : keys)
    {
        node = node->find(row.subspan(key.offset, key.size));
        if (!node)
            return false;
    }
    return node->rows != 0;
}
}
}

namespace ygg::database
{
namespace detail
{
bool GenericJoinTrie::insert(std::span<const std::byte> row, std::span<const ColumnSlice> keys)
{
    if (keys.empty())
    {
        const bool inserted = rows == 0;
        rows = 1;
        return inserted;
    }
    const auto value = row.subspan(keys.front().offset, keys.front().size);
    auto found = children.find(std::make_tuple(value));
    if (found == children.end())
        found = children.emplace(GenericJoinKey { { value.begin(), value.end() } }, std::make_unique<GenericJoinTrie>()).first;
    if (!found->second->insert(row, keys.subspan(1)))
        return false;
    ++rows;
    return true;
}
}
}

namespace ygg::database
{
namespace detail
{
bool GenericJoinTrie::erase(std::span<const std::byte> row, std::span<const ColumnSlice> keys)
{
    if (keys.empty())
    {
        const bool erased = rows != 0;
        rows = 0;
        return erased;
    }
    const auto value = row.subspan(keys.front().offset, keys.front().size);
    const auto found = children.find(std::make_tuple(value));
    if (found == children.end() || !found->second->erase(row, keys.subspan(1)))
        return false;
    --rows;
    if (found->second->rows == 0)
        children.erase(found);
    return true;
}
}
}

namespace ygg::database
{
void GenericJoinWorkspace::enumerate(size_t depth, Builder<Relation>& out)
{
    detail::count_generic_join_work(&detail::GenericJoinWork::prefixes);
    if (depth == m_plan.variable_order().size())
    {
        out.insert(Row(m_row, m_plan.output_columns().span()));
        return;
    }
    const auto count = m_plan.input_count();
    const auto* current = m_cursors.data() + depth * count;
    auto* next = m_cursors.data() + (depth + 1) * count;
    const auto inputs = m_plan.variable_inputs(depth);
    const auto source = *std::ranges::min_element(inputs, {}, [&](size_t input) { return current[input]->children.size(); });
    for (const auto& [key, child] : current[source]->children)
    {
        detail::count_generic_join_work(&detail::GenericJoinWork::candidate_keys);
        std::copy_n(current, count, next);
        bool present = true;
        for (const auto input : inputs)
        {
            if (input != source)
                detail::count_generic_join_work(&detail::GenericJoinWork::probes);
            next[input] = input == source ? child.get() : current[input]->find(key.bytes);
            if (!next[input])
            {
                present = false;
                break;
            }
        }
        if (present)
        {
            const auto output = m_plan.output_position(depth);
            std::ranges::copy(key.bytes, m_row.begin() + output.offset);
            enumerate(depth + 1, out);
        }
    }
}
}
