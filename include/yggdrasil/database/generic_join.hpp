/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_GENERIC_JOIN_HPP_
#define YGG_DATABASE_GENERIC_JOIN_HPP_

#include "yggdrasil/database/operations.hpp"

#include <cista/containers/vector.h>
#include <memory>
#include <tuple>

namespace ygg::database
{
/// Prepared full natural join. Variable order controls execution; output order
/// independently preserves the caller's schema. Projection is a separate operator.
template<ColumnTypes Values = DefaultColumnTypes>
class GenericJoinPlan
{
    cista::offset::vector<cista::offset::vector<ColumnLayout>> m_inputs;
    Builder<Columns<Values>> m_output;
    cista::offset::vector<Index<Column>> m_order;
    cista::offset::vector<cista::offset::vector<ColumnSlice>> m_keys;
    cista::offset::vector<cista::offset::vector<size_t>> m_occurrences;
    cista::offset::vector<ColumnSlice> m_output_positions;

public:
    GenericJoinPlan() = default;
    GenericJoinPlan(std::span<const std::vector<ColumnLayout>> inputs,
                    std::span<const Index<Column>> variable_order,
                    std::span<const Index<Column>> output_order) :
        m_inputs(inputs.size()),
        m_order(variable_order.begin(), variable_order.end()),
        m_keys(inputs.size()),
        m_occurrences(variable_order.size())
    {
        for (size_t i = 0; i < inputs.size(); ++i)
            m_inputs[i].set(inputs[i].begin(), inputs[i].end());
        std::vector<ColumnLayout> combined;
        for (const auto& input : inputs)
        {
            validate_columns<Values>(input);
            for (const auto column : input)
            {
                const auto found = std::ranges::find(combined, column.label, &ColumnLayout::label);
                if (found == combined.end())
                    detail::append_column(combined, column);
                else if (found->type != column.type || found->size != column.size)
                    throw std::invalid_argument("Generic join: common columns must have the same type.");
            }
        }
        const auto require_order = [&](std::span<const Index<Column>> order)
        {
            if (order.size() != combined.size())
                throw std::invalid_argument("Generic join: orders must contain every output column exactly once.");
            for (size_t i = 0; i < order.size(); ++i)
                if (std::ranges::find(combined, order[i], &ColumnLayout::label) == combined.end()
                    || std::ranges::find(order.first(i), order[i]) != order.first(i).end())
                    throw std::invalid_argument("Generic join: orders must contain every output column exactly once.");
        };
        require_order(variable_order);
        require_order(output_order);
        std::vector<ColumnLayout> output;
        for (const auto label : output_order)
            detail::append_column(output, combined[database::column_index(combined, label)]);
        m_output.assign(output);
        for (size_t depth = 0; depth < variable_order.size(); ++depth)
        {
            const auto label = variable_order[depth];
            m_output_positions.push_back(detail::column_slice(output[database::column_index(output, label)]));
            for (size_t input = 0; input < inputs.size(); ++input)
            {
                const auto found = std::ranges::find(inputs[input], label, &ColumnLayout::label);
                if (found != inputs[input].end())
                {
                    m_keys[input].push_back(detail::column_slice(*found));
                    m_occurrences[depth].push_back(input);
                }
            }
        }
    }

    size_t input_count() const noexcept { return m_inputs.size(); }
    std::span<const cista::offset::vector<ColumnLayout>> input_schemas() const noexcept { return { m_inputs.data(), m_inputs.size() }; }
    std::span<const ColumnLayout> input_columns(size_t input) const
    {
        const auto& columns = m_inputs.at(input);
        return { columns.data(), columns.size() };
    }
    const auto& output_columns() const& noexcept { return m_output; }
    const auto& output_columns() const&& = delete;
    std::span<const Index<Column>> variable_order() const noexcept { return { m_order.data(), m_order.size() }; }
    std::span<const ColumnSlice> input_keys(size_t input) const
    {
        const auto& keys = m_keys.at(input);
        return { keys.data(), keys.size() };
    }
    std::span<const size_t> variable_inputs(size_t depth) const
    {
        const auto& occurrences = m_occurrences.at(depth);
        return { occurrences.data(), occurrences.size() };
    }
    ColumnSlice output_position(size_t depth) const { return m_output_positions.at(depth); }

    auto cista_members() noexcept { return std::tie(m_inputs, m_output, m_order, m_keys, m_occurrences, m_output_positions); }
    auto cista_members() const noexcept { return std::tie(m_inputs, m_output, m_order, m_keys, m_occurrences, m_output_positions); }

    bool matches(const GenericJoinPlan& other) const
    {
        return m_inputs == other.m_inputs && m_order == other.m_order && std::ranges::equal(m_output.span(), other.m_output.span());
    }
};

namespace detail
{
// Test targets enable these counters consistently across all translation units.
struct GenericJoinWork
{
    size_t candidate_keys = 0;
    size_t probes = 0;
    size_t prefixes = 0;
};
#ifdef YGG_GENERIC_JOIN_INSTRUMENTATION
inline thread_local GenericJoinWork generic_join_work;
#endif
inline void count_generic_join_work([[maybe_unused]] size_t GenericJoinWork::*member) noexcept
{
#ifdef YGG_GENERIC_JOIN_INSTRUMENTATION
    ++(generic_join_work.*member);
#endif
}

struct GenericJoinKey
{
    std::vector<std::byte> bytes;
    auto identifying_members() const noexcept { return std::make_tuple(std::span<const std::byte>(bytes)); }
};

/// Hash-trie nodes own distinct canonical values; input row movement is irrelevant.
struct GenericJoinTrie
{
    UnorderedMap<GenericJoinKey, std::unique_ptr<GenericJoinTrie>> children;
    size_t rows = 0;

    const GenericJoinTrie* find(std::span<const std::byte> value) const
    {
        const auto found = children.find(std::make_tuple(value));
        return found == children.end() ? nullptr : found->second.get();
    }

    bool contains(std::span<const std::byte> row, std::span<const ColumnSlice> keys) const
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

    bool insert(std::span<const std::byte> row, std::span<const ColumnSlice> keys)
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

    bool erase(std::span<const std::byte> row, std::span<const ColumnSlice> keys)
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

    void clear()
    {
        children.clear();
        rows = 0;
    }

    size_t memory_usage() const noexcept
    {
        size_t result = children.capacity() * (sizeof(decltype(children)::value_type) + sizeof(gtl::priv::ctrl_t));
        for (const auto& [key, child] : children)
            result += key.bytes.capacity() + sizeof(GenericJoinTrie) + child->memory_usage();
        return result;
    }
};
}  // namespace detail

/// Sequential scratch and hash tries for one prepared plan. Owns indexed bytes;
/// sources need only remain valid during the call. Moving preserves usability.
template<ColumnTypes Values = DefaultColumnTypes>
class GenericJoinWorkspace
{
    GenericJoinPlan<Values> m_plan;
    std::vector<detail::GenericJoinTrie> m_tries;
    std::vector<const detail::GenericJoinTrie*> m_cursors;
    std::vector<std::byte> m_row;

    void enumerate(size_t depth, Builder<Relation<Values>>& out)
    {
        detail::count_generic_join_work(&detail::GenericJoinWork::prefixes);
        if (depth == m_plan.variable_order().size())
        {
            out.insert(Row<Values>(m_row, m_plan.output_columns().span()));
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

public:
    explicit GenericJoinWorkspace(const GenericJoinPlan<Values>& plan) :
        m_plan(plan),
        m_tries(plan.input_count()),
        m_cursors((plan.variable_order().size() + 1) * plan.input_count()),
        m_row(plan.output_columns().row_size())
    {
    }

    bool matches(const GenericJoinPlan<Values>& plan) const { return m_plan.matches(plan); }
    detail::GenericJoinTrie& trie(size_t input) { return m_tries.at(input); }
    const detail::GenericJoinTrie& trie(size_t input) const { return m_tries.at(input); }

    /// Append full assignments; optionally replace one occurrence by a delta trie.
    void append(Builder<Relation<Values>>& out, size_t replaced = std::numeric_limits<size_t>::max(), const detail::GenericJoinTrie* replacement = nullptr)
    {
        for (size_t input = 0; input < m_tries.size(); ++input)
        {
            const auto* root = input == replaced ? replacement : &m_tries[input];
            if (!root || root->rows == 0)
                return;
            m_cursors[input] = root;
        }
        enumerate(0, out);
    }

    size_t memory_usage() const noexcept
    {
        size_t result = m_tries.capacity() * sizeof(detail::GenericJoinTrie) + m_cursors.capacity() * sizeof(m_cursors[0]) + m_row.capacity();
        for (const auto& trie : m_tries)
            result += trie.memory_usage();
        return result;
    }
};

/// Generic Join (Ngo, Re, Rudra, 2013, Section 4): intersect one variable at a
/// time, iterating the smallest distinct prefix extension and probing the rest.
/// https://arxiv.org/abs/1310.3314. Hash costs are expected; no ordering is required.
/// Output replaces its rows; schema/alias/plan errors precede mutation.
template<ColumnTypes Values, RelationViewRange<Values> R>
void generic_join(const R& inputs,
                  const GenericJoinPlan<Values>& plan,
                  Builder<Relation<Values>>& out,
                  GenericJoinWorkspace<Values>& workspace)
{
    if (std::ranges::size(inputs) != plan.input_count() || !workspace.matches(plan))
        throw std::invalid_argument("Generic join: inputs or workspace do not match the plan.");
    detail::require_plan_columns(out.columns().span(), plan.output_columns().span());
    for (size_t input = 0; input < plan.input_count(); ++input)
    {
        detail::require_plan_columns(inputs[input].columns().span(), plan.input_columns(input));
        if (inputs[input].get_storage_address() == out.get_storage_address())
            throw std::invalid_argument("Generic join: output aliases an input.");
    }
    out.clear();
    for (size_t input = 0; input < plan.input_count(); ++input)
    {
        auto& trie = workspace.trie(input);
        trie.clear();
        for (size_t row = 0; row < inputs[input].size(); ++row)
            trie.insert(inputs[input].row(row), plan.input_keys(input));
    }
    workspace.append(out);
}
}  // namespace ygg::database

#endif
