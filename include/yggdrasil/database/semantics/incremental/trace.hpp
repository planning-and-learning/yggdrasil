/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_SEMANTICS_INCREMENTAL_TRACE_HPP_
#define YGG_DATABASE_SEMANTICS_INCREMENTAL_TRACE_HPP_

#include "yggdrasil/containers/unordered_multi_map.hpp"
#include "yggdrasil/database/semantics/join_index.hpp"
#include "yggdrasil/database/semantics/zset.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace ygg::database::incremental
{
/// An iteration of a fixpoint, the inner time of a nested stream [Budiu23, §6].
using Iteration = uint32_t;

/// Rows with a nonzero weight per iteration, indexed by the hash of some key columns. A nested stream's changes over
/// (update, iteration) time are kept with the updates summed, as Feldera's traces do once each update ends.
class TimedRows
{
public:
    struct Entry
    {
        Iteration iteration;
        Weight weight;
    };

    TimedRows(std::span<const ColumnLayout> columns, std::span<const ColumnSlice> keys);

    /// Adds `weight` to the row's weight at `iteration`.
    void add(std::span<const std::byte> row, Iteration iteration, Weight weight);
    /// The row's nonzero weights in increasing iteration order (empty if it has none).
    [[nodiscard]] std::span<const Entry> entries(std::span<const std::byte> row) const;
    /// Calls f(row, entries) for each row whose key columns equal the probe row's `probe_keys`.
    template<typename F>
    void for_each_match(std::span<const std::byte> probe, std::span<const ColumnSlice> probe_keys, const F& f) const
    {
        if (m_keys.empty())
        {
            for (size_t i = 0; i < m_rows.size(); ++i)
                f(m_rows.row(i), std::span<const Entry>(m_entries[i]));
            return;
        }
        for (const auto position : m_index.values(database::detail::join_key_hash(probe, probe_keys)))
            if (database::detail::join_keys_equal(m_rows.row(position), m_keys, probe, probe_keys))
                f(m_rows.row(position), std::span<const Entry>(m_entries[position]));
    }
    /// Adds every row's weights into `other` and clears this.
    void move_into(TimedRows& other);
    [[nodiscard]] bool empty() const noexcept { return m_rows.empty(); }
    void clear() noexcept;
    [[nodiscard]] size_t memory_usage() const noexcept;

    /// The sum of the weights at iterations up to `iteration`.
    [[nodiscard]] static Weight up_to(std::span<const Entry> entries, Iteration iteration) noexcept;

private:
    Builder<Relation> m_rows;
    std::vector<std::vector<Entry>> m_entries;
    std::vector<ColumnSlice> m_keys;
    UnorderedMultiMap<hash_t, uint_t> m_index;

    void erase(uint_t position);
};

/// The history of a nested stream: the earlier updates' changes (summed) and the current update's, kept apart because
/// incremental operators combine a change with each of them differently.
class Trace
{
    TimedRows m_past;
    TimedRows m_current;

public:
    Trace(std::span<const ColumnLayout> columns, std::span<const ColumnSlice> keys);

    [[nodiscard]] const TimedRows& past() const noexcept { return m_past; }
    [[nodiscard]] const TimedRows& current() const noexcept { return m_current; }
    /// Records the stream's change at `iteration` of the current update.
    void add(const ZSet& change, Iteration iteration);
    /// Ends the current update: its changes become part of the past.
    void commit() { m_current.move_into(m_past); }
    void clear() noexcept;
    [[nodiscard]] size_t memory_usage() const noexcept { return m_past.memory_usage() + m_current.memory_usage(); }
};
}  // namespace ygg::database::incremental

#endif
