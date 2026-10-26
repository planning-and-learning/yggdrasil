/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yggdrasil/database/semantics/aggregate.hpp"

#include <stdexcept>

namespace ygg::database
{
AggregatePlan::AggregatePlan(std::span<const ColumnLayout> arg,
                             std::span<const ColumnLayout> groups,
                             AggregateFunction function,
                             Index<Column> value,
                             Index<Column> result) :
    m_arg(arg),
    m_groups(groups),
    m_output(groups),
    m_function(function)
{
    for (const auto& column : groups)
    {
        const auto& source = arg[column_index(arg, column.label)];
        if (source.type != column.type)
            throw std::invalid_argument("Aggregate: group columns must have the same type in both inputs.");
        m_keys.push_back(detail::column_slice(source));
    }
    if (function != AggregateFunction::COUNT)
    {
        const auto& source = arg[column_index(arg, value)];
        if (source.type != type_id<uint_t>)
            throw std::invalid_argument("Aggregate: the value column must have type uint_t.");
        m_value = detail::column_slice(source);
    }
    if (std::ranges::find(groups, result, &ColumnLayout::label) != groups.end())
        throw std::invalid_argument("Aggregate: the result column must not be a group column.");
    m_output.push_back(result, type_id<uint_t>);
}

GroupedAggregate::GroupedAggregate(AggregateFunction function, std::span<const ColumnLayout> key_columns) : m_function(function), m_keys(key_columns) {}

void GroupedAggregate::add(std::span<const std::byte> key, uint_t value, Weight weight)
{
    auto position = m_keys.find(key);
    if (!position)
    {
        position = m_keys.insert(key);
        m_groups.emplace_back();
    }
    auto& group = m_groups[*position];
    group.count += weight;
    group.sum += static_cast<uint64_t>(weight) * value;
    if (m_function == AggregateFunction::MIN || m_function == AggregateFunction::MAX)
    {
        auto& count = group.values[value];
        count += weight;
        if (count == 0)
            group.values.erase(value);
    }
    if (group.count == 0)
    {
        m_keys.erase(*position);
        m_groups[*position] = std::move(m_groups.back());
        m_groups.pop_back();
    }
}

std::optional<uint_t> GroupedAggregate::get(std::span<const std::byte> key) const
{
    const auto position = m_keys.find(key);
    if (!position)
        return m_function == AggregateFunction::COUNT || m_function == AggregateFunction::SUM ? std::optional<uint_t>(0) : std::nullopt;
    const auto& group = m_groups[*position];
    switch (m_function)
    {
        case AggregateFunction::COUNT:
            return to_uint_t(static_cast<size_t>(group.count));
        case AggregateFunction::SUM:
            return static_cast<uint_t>(group.sum);
        case AggregateFunction::MIN:
            return group.values.begin()->first;
        case AggregateFunction::MAX:
            return group.values.rbegin()->first;
    }
    return std::nullopt;
}

void GroupedAggregate::clear() noexcept
{
    m_keys.clear();
    m_groups.clear();
}

size_t GroupedAggregate::memory_usage() const noexcept
{
    size_t result = m_keys.memory_usage() + m_groups.capacity() * sizeof(Group);
    for (const auto& group : m_groups)
        result += group.values.size() * (sizeof(uint_t) + sizeof(Weight) + 4 * sizeof(void*));
    return result;
}

namespace detail
{
std::span<const std::byte> aggregate_key(std::span<const std::byte> row, const AggregatePlan& plan, std::vector<std::byte>& buffer)
{
    buffer.clear();
    append_fields(buffer, row, plan.keys());
    return buffer;
}

uint_t aggregate_value(std::span<const std::byte> row, const AggregatePlan& plan)
{
    if (plan.function() == AggregateFunction::COUNT)
        return 0;
    return ByteCodec<uint_t>::decode(row.subspan(plan.value().offset, plan.value().size));
}

std::span<const std::byte> aggregate_row(std::span<const std::byte> key, uint_t value, std::vector<std::byte>& buffer)
{
    buffer.assign(key.begin(), key.end());
    buffer.resize(key.size() + ByteCodec<uint_t>::size);
    ByteCodec<uint_t>::encode(value, std::span(buffer).last(ByteCodec<uint_t>::size));
    return buffer;
}
}  // namespace detail
}  // namespace ygg::database
