/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_OPERATORS_GENERIC_JOIN_GENERIC_JOIN_HPP_
#define YGG_DATABASE_OPERATORS_GENERIC_JOIN_GENERIC_JOIN_HPP_

#include "yggdrasil/containers/associative_containers.hpp"
#include "yggdrasil/database/operators/generic_join/plan.hpp"
#include "yggdrasil/database/operators/relational/common.hpp"

#include <cista/containers/vector.h>
#include <memory>
#include <tuple>

namespace ygg::database
{
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
inline GenericJoinWork& generic_join_work() noexcept
{
    thread_local GenericJoinWork work;
    return work;
}
#endif
void count_generic_join_work([[maybe_unused]] size_t GenericJoinWork::* member) noexcept;

struct GenericJoinKey
{
    std::vector<std::byte> bytes;
    [[nodiscard]] auto identifying_members() const noexcept { return std::make_tuple(std::span<const std::byte>(bytes)); }
};

/// Hash-trie nodes own distinct canonical values; input row movement is irrelevant.
struct GenericJoinTrie
{
    UnorderedMap<GenericJoinKey, std::unique_ptr<GenericJoinTrie>> children;
    size_t rows = 0;

    [[nodiscard]] const GenericJoinTrie* find(std::span<const std::byte> value) const
    {
        const auto found = children.find(std::make_tuple(value));
        return found == children.end() ? nullptr : found->second.get();
    }

    [[nodiscard]] bool contains(std::span<const std::byte> row, std::span<const ColumnSlice> keys) const;

    bool insert(std::span<const std::byte> row, std::span<const ColumnSlice> keys);

    bool erase(std::span<const std::byte> row, std::span<const ColumnSlice> keys);

    void clear()
    {
        children.clear();
        rows = 0;
    }

    [[nodiscard]] size_t memory_usage() const noexcept
    {
        size_t result = children.capacity() * (sizeof(std::pair<const GenericJoinKey, std::unique_ptr<GenericJoinTrie>>) + sizeof(gtl::priv::ctrl_t));
        for (const auto& [key, child] : children)
            result += key.bytes.capacity() + sizeof(GenericJoinTrie) + child->memory_usage();
        return result;
    }
};
}  // namespace detail

namespace incremental
{
class GenericJoinEvaluator;
}

/// Sequential scratch and hash tries for one prepared plan. Owns indexed bytes;
/// sources need only remain valid during the call. Moving preserves usability.
class GenericJoinWorkspace
{
    GenericJoinPlan m_plan;
    std::vector<detail::GenericJoinTrie> m_tries;
    std::vector<const detail::GenericJoinTrie*> m_cursors;
    std::vector<std::byte> m_row;

    void enumerate(size_t depth, Builder<Relation>& out);

    /// Appends the full assignments over the tries returned by root(input).
    template<std::invocable<size_t> Root>
    void append(Builder<Relation>& out, Root root)
    {
        for (size_t input = 0; input < m_tries.size(); ++input)
        {
            const detail::GenericJoinTrie& trie = root(input);
            if (trie.rows == 0)
                return;
            m_cursors[input] = &trie;
        }
        enumerate(0, out);
    }
    void append(Builder<Relation>& out)
    {
        append(out, [&](size_t input) -> const auto& { return m_tries[input]; });
    }
    /// Appends the assignments that read the replacement trie for the replaced input.
    void append(Builder<Relation>& out, size_t replaced, const detail::GenericJoinTrie& replacement)
    {
        append(out, [&](size_t input) -> const auto& { return input == replaced ? replacement : m_tries[input]; });
    }
    detail::GenericJoinTrie& trie(size_t input) { return m_tries.at(input); }

    friend class incremental::GenericJoinEvaluator;

public:
    explicit GenericJoinWorkspace(const GenericJoinPlan& plan) :
        m_plan(plan),
        m_tries(plan.input_count()),
        m_cursors((plan.variable_order().size() + 1) * plan.input_count()),
        m_row(plan.output_columns().row_size())
    {
    }

    [[nodiscard]] bool matches(const GenericJoinPlan& plan) const { return m_plan.matches(plan); }

    /// Indexes the inputs, which match the plan, and appends their join to out.
    template<RelationViewRange R>
    void append_join(const R& inputs, Builder<Relation>& out)
    {
        for (size_t input = 0; input < m_plan.input_count(); ++input)
        {
            auto& trie = m_tries[input];
            trie.clear();
            for (size_t row = 0; row < inputs[input].size(); ++row)
                trie.insert(inputs[input].row(row), m_plan.input_keys(input));
        }
        append(out);
    }

    [[nodiscard]] size_t memory_usage() const noexcept
    {
        size_t result =
            m_tries.capacity() * sizeof(detail::GenericJoinTrie) + m_cursors.capacity() * sizeof(typename decltype(m_cursors)::value_type) + m_row.capacity();
        for (const auto& trie : m_tries)
            result += trie.memory_usage();
        return result;
    }
};

/// Generic Join (Ngo, Re, Rudra, 2013, Section 4): intersect one variable at a
/// time, iterating the smallest distinct prefix extension and probing the rest.
/// https://arxiv.org/abs/1310.3314. Hash costs are expected; no ordering is required.
/// Output replaces its rows; schema/alias/plan errors precede mutation.
template<RelationViewRange R>
void generic_join(const R& inputs, const GenericJoinPlan& plan, Builder<Relation>& out, GenericJoinWorkspace& workspace)
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
    workspace.append_join(inputs, out);
}
}  // namespace ygg::database

#endif
