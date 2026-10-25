/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_DATALOG_DETAILS_TABLE_HPP_
#define YGG_DATABASE_DATALOG_DETAILS_TABLE_HPP_

#include "yggdrasil/core/types.hpp"
#include "yggdrasil/database/datalog/details/tuples.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace ygg::database::datalog::detail
{
constexpr size_t npos = std::numeric_limits<size_t>::max();

/// Which rows of a table a literal reads.
enum class Version : uint8_t
{
    NEW,
    /// The rows before the update: the current ones without the added, with the removed.
    OLD,
    ADDED,
    REMOVED,
    /// The rows found last by a semi-naive iteration (insertion or over-deletion).
    FRONTIER,
};

/// The rows of a relation (an input, a derived relation, or an aggregate's body or groups), its changes of the current
/// update, and the staging sets of semi-naive iterations, all indexed on the same access patterns.
struct Table
{
    size_t arity;
    std::optional<size_t> slot;
    bool is_static = false;
    bool loaded = false;
    IndexedTuples current, added, removed, frontier, next;
    /// The derivation count of each row, for strata without recursion.
    bool counted = false;
    TupleSet count_rows;
    std::vector<int64_t> counts;
    TupleSet pending_rows;
    /// The rows before a recursive stratum is evaluated anew.
    TupleSet snapshot;
    std::vector<int64_t> pending;
    /// An aggregate's value of an absent group (COUNT and SUM: 0); its last column holds the value.
    std::optional<uint_t> fallback;

    explicit Table(size_t arity_) :
        arity(arity_),
        current(arity_),
        added(arity_),
        removed(arity_),
        frontier(arity_),
        next(arity_),
        count_rows(arity_),
        pending_rows(arity_),
        snapshot(arity_)
    {
    }

    size_t add_index(const std::vector<size_t>& columns)
    {
        const auto index = current.add_index(columns);
        for (auto* set : { &added, &removed, &frontier, &next })
            set->add_index(columns);
        return index;
    }
    [[nodiscard]] bool changed() const noexcept { return !added.empty() || !removed.empty(); }
    void clear_changes() noexcept
    {
        added.clear();
        removed.clear();
    }
    void clear() noexcept
    {
        for (auto* set : { &current, &added, &removed, &frontier, &next })
            set->clear();
        count_rows.clear();
        counts.clear();
    }

    template<typename F>
    void lookup(Version version, size_t index, std::span<const uint_t> key, const F& f) const
    {
        const auto each = [&](const IndexedTuples& rows, auto&& g)
        {
            if (index == npos)
                rows.for_each(g);
            else
                rows.for_each_match(index, key, g);
        };
        switch (version)
        {
            case Version::NEW:
                return each(current, f);
            case Version::ADDED:
                return each(added, f);
            case Version::REMOVED:
                return each(removed, f);
            case Version::FRONTIER:
                return each(frontier, f);
            case Version::OLD:
                each(current,
                     [&](std::span<const uint_t> row)
                     {
                         if (!added.contains(row))
                             f(row);
                     });
                return each(removed, f);
        }
    }
    /// About the number of rows lookup() visits.
    [[nodiscard]] size_t count(Version version, size_t index, std::span<const uint_t> key) const
    {
        switch (version)
        {
            case Version::NEW:
                return current.count(index, key);
            case Version::ADDED:
                return added.count(index, key);
            case Version::REMOVED:
                return removed.count(index, key);
            case Version::FRONTIER:
                return frontier.count(index, key);
            case Version::OLD:
                return current.count(index, key) + removed.count(index, key);
        }
        return 0;
    }
    [[nodiscard]] bool contains(Version version, std::span<const uint_t> row) const
    {
        switch (version)
        {
            case Version::NEW:
                return current.contains(row);
            case Version::ADDED:
                return added.contains(row);
            case Version::REMOVED:
                return removed.contains(row);
            case Version::FRONTIER:
                return frontier.contains(row);
            case Version::OLD:
                return (current.contains(row) && !added.contains(row)) || removed.contains(row);
        }
        return false;
    }
    [[nodiscard]] size_t memory_usage() const noexcept
    {
        size_t result =
            count_rows.memory_usage() + pending_rows.memory_usage() + snapshot.memory_usage() + (counts.capacity() + pending.capacity()) * sizeof(int64_t);
        for (const auto* set : { &current, &added, &removed, &frontier, &next })
            result += set->memory_usage();
        return result;
    }
};
}  // namespace ygg::database::datalog::detail

#endif
