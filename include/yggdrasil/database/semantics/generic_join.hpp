/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_GENERIC_JOIN_HPP_
#define YGG_DATABASE_SEMANTICS_GENERIC_JOIN_HPP_

#include "yggdrasil/database/semantics/generic_join_plan.hpp"
#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>
#include "yggdrasil/database/semantics/operations.hpp"

#include <urd/memory.hpp>
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
    urd::Store<urd::sequence<std::byte>> bytes;
    /// The key as a span: lookups probe with a std::tuple of one, holding no key.
    [[nodiscard]] std::span<const std::byte> byte_span() const noexcept { return bytes; }
    static constexpr auto identifying_members() noexcept { return urd::Members<&GenericJoinKey::byte_span> {}; }
};

struct GenericJoinTrie;
}  // namespace detail
}  // namespace ygg::database

/// A key owns its bytes; a trie node what its memory_usage() counts (its table, keys and children).
template<>
struct urd::memory_of<ygg::database::detail::GenericJoinKey>
{
    static std::size_t bytes(const ygg::database::detail::GenericJoinKey& key) { return urd::memory_usage(key.bytes); }
};
template<>
struct urd::memory_of<ygg::database::detail::GenericJoinTrie>
{
    static std::size_t bytes(const ygg::database::detail::GenericJoinTrie& trie);
};

namespace ygg::database
{
namespace detail
{

/// Hash-trie nodes own distinct canonical values; input row movement is irrelevant.
struct GenericJoinTrie
{
    urd::Store<urd::map<GenericJoinKey, std::unique_ptr<GenericJoinTrie>>> children;
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

    /// Its table, and each key's bytes and child node.
    [[nodiscard]] size_t memory_usage() const noexcept { return urd::memory_usage(children); }
};
}  // namespace detail
}  // namespace ygg::database

inline std::size_t urd::memory_of<ygg::database::detail::GenericJoinTrie>::bytes(const ygg::database::detail::GenericJoinTrie& trie)
{ return trie.memory_usage(); }

namespace ygg::database
{

namespace incremental
{
class GenericJoinEvaluator;
}

/// Sequential scratch and hash tries for one prepared plan. Owns indexed bytes;
/// sources need only remain valid during the call. Moving preserves usability.
class GenericJoinWorkspace
{
    GenericJoinPlan m_plan;
    urd::Store<urd::sequence<detail::GenericJoinTrie>> m_tries;
    urd::Store<urd::sequence<const detail::GenericJoinTrie*>> m_cursors;
    urd::Store<urd::sequence<std::byte>> m_row;

    void enumerate(size_t depth, urd::builder<Relation>& out);

    /// Appends the full assignments over the tries returned by root(input).
    template<std::invocable<size_t> Root>
    void append(urd::builder<Relation>& out, Root root)
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
    void append(urd::builder<Relation>& out)
    {
        append(out, [&](size_t input) -> const auto& { return m_tries[input]; });
    }
    /// Appends the assignments that read the replacement trie for the replaced input.
    void append(urd::builder<Relation>& out, size_t replaced, const detail::GenericJoinTrie& replacement)
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
    void append_join(const R& inputs, urd::builder<Relation>& out)
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
void generic_join(const R& inputs, const GenericJoinPlan& plan, urd::builder<Relation>& out, GenericJoinWorkspace& workspace)
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
