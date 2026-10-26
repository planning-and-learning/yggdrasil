/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_DATALOG_DETAILS_TUPLES_HPP_
#define YGG_DATABASE_DATALOG_DETAILS_TUPLES_HPP_

#include "yggdrasil/containers/raw_array_set.hpp"
#include "yggdrasil/core/types.hpp"
#include "yggdrasil/semantics/hash.hpp"

#include <cstddef>
#include <gtl/phmap.hpp>
#include <limits>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

/// Row storage of the evaluator (evaluator.hpp): fixed-arity tuples of object ids.
namespace ygg::database::datalog::detail
{
/// The rows of a tuple set by the hash of some columns (an access pattern): per key hash a doubly linked list of row
/// positions and their number, so inserting and removing a row are O(1) however many rows share its key.
class TupleIndex
{
    static constexpr uint_t none = std::numeric_limits<uint_t>::max();

    struct Head
    {
        uint_t first;
        uint_t count;
    };

    std::vector<size_t> m_columns;
    gtl::flat_hash_map<hash_t, Head> m_heads;
    std::vector<uint_t> m_next;
    std::vector<uint_t> m_previous;
    std::vector<hash_t> m_hashes;

    void link(uint_t position, uint_t previous, uint_t next, hash_t hash)
    {
        if (previous == none)
            m_heads[hash].first = position;
        else
            m_next[previous] = position;
        if (next != none)
            m_previous[next] = position;
    }

public:
    explicit TupleIndex(std::vector<size_t> columns) : m_columns(std::move(columns)) {}

    [[nodiscard]] const std::vector<size_t>& columns() const noexcept { return m_columns; }
    [[nodiscard]] hash_t hash(std::span<const uint_t> row) const noexcept
    {
        return hash_range(m_columns | std::views::transform([&](size_t column) { return row[column]; }));
    }
    /// Indexes the row at `position`, the next position of the set.
    void insert(uint_t position, std::span<const uint_t> row)
    {
        const auto key = hash(row);
        const auto [it, inserted] = m_heads.try_emplace(key, Head { position, 0 });
        const auto next = inserted ? none : it->second.first;
        if (!inserted)
        {
            m_previous[next] = position;
            it->second.first = position;
        }
        ++it->second.count;
        m_next.push_back(next);
        m_previous.push_back(none);
        m_hashes.push_back(key);
    }
    /// Removes the row at `position`, then moves the set's last row (`last`) into it, as RawArraySet::erase does.
    void erase(uint_t position, uint_t last)
    {
        const auto previous = m_previous[position], next = m_next[position];
        const auto head = m_heads.find(m_hashes[position]);
        if (--head->second.count == 0)
            m_heads.erase(head);
        else if (previous == none)
            head->second.first = next;
        if (previous != none)
            m_next[previous] = next;
        if (next != none)
            m_previous[next] = previous;
        if (position != last)
        {
            m_previous[position] = m_previous[last];
            m_next[position] = m_next[last];
            m_hashes[position] = m_hashes[last];
            link(position, m_previous[position], m_next[position], m_hashes[position]);
        }
        m_next.pop_back();
        m_previous.pop_back();
        m_hashes.pop_back();
    }
    void clear() noexcept
    {
        m_heads.clear();
        m_next.clear();
        m_previous.clear();
        m_hashes.clear();
    }
    /// Calls f(position) for the rows whose key hashes like `key`'s values.
    template<typename F>
    void for_each_candidate(std::span<const uint_t> key, const F& f) const
    {
        const auto it = m_heads.find(hash_range(key));
        if (it == m_heads.end())
            return;
        for (auto position = it->second.first; position != none; position = m_next[position])
            f(position);
    }
    /// The number of rows whose key hashes like `key`'s values.
    [[nodiscard]] size_t count(std::span<const uint_t> key) const
    {
        const auto it = m_heads.find(hash_range(key));
        return it == m_heads.end() ? 0 : it->second.count;
    }
    [[nodiscard]] size_t memory_usage() const noexcept
    {
        return m_heads.capacity() * (sizeof(hash_t) + sizeof(Head) + 1) + (m_next.capacity() + m_previous.capacity()) * sizeof(uint_t)
               + m_hashes.capacity() * sizeof(hash_t);
    }
};

/// A tuple set with indexes on column subsets (access patterns), kept valid under erase.
class IndexedTuples
{
    RawArraySet<uint_t> m_rows;
    std::vector<TupleIndex> m_indexes;

public:
    explicit IndexedTuples(size_t arity) : m_rows(arity) {}

    [[nodiscard]] const RawArraySet<uint_t>& rows() const noexcept { return m_rows; }
    [[nodiscard]] size_t size() const noexcept { return m_rows.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_rows.empty(); }
    [[nodiscard]] bool contains(std::span<const uint_t> row) const { return m_rows.contains(row); }
    std::span<const uint_t> operator[](uint_t index) const noexcept { return m_rows[index]; }
    [[nodiscard]] const std::vector<size_t>& columns(size_t index) const noexcept { return m_indexes[index].columns(); }

    /// The index on these columns, made if new; existing rows are indexed.
    size_t add_index(const std::vector<size_t>& columns)
    {
        for (size_t i = 0; i < m_indexes.size(); ++i)
            if (m_indexes[i].columns() == columns)
                return i;
        auto& index = m_indexes.emplace_back(columns);
        for (uint_t row = 0; row < m_rows.size(); ++row)
            index.insert(row, m_rows[row]);
        return m_indexes.size() - 1;
    }
    bool insert(std::span<const uint_t> row)
    {
        const auto size = m_rows.size();
        const auto position = m_rows.insert(row);
        if (m_rows.size() == size)
            return false;
        for (auto& index : m_indexes)
            index.insert(position, row);
        return true;
    }
    bool erase(std::span<const uint_t> row)
    {
        const auto position = m_rows.find(row);
        if (!position)
            return false;
        const auto last = to_uint_t(m_rows.size() - 1);
        for (auto& index : m_indexes)
            index.erase(*position, last);
        m_rows.erase(*position);
        return true;
    }
    void clear() noexcept
    {
        m_rows.clear();
        for (auto& index : m_indexes)
            index.clear();
    }
    /// About the number of rows for_each_match visits: all rows without an index.
    [[nodiscard]] size_t count(size_t index, std::span<const uint_t> key) const
    { return index == std::numeric_limits<size_t>::max() ? m_rows.size() : m_indexes[index].count(key); }
    /// Calls f(row) for every row whose index columns equal `key`.
    template<typename F>
    void for_each_match(size_t index, std::span<const uint_t> key, const F& f) const
    {
        const auto& columns = m_indexes[index].columns();
        m_indexes[index].for_each_candidate(key,
                                            [&](uint_t position)
                                            {
                                                const auto row = m_rows[position];
                                                for (size_t i = 0; i < columns.size(); ++i)
                                                    if (row[columns[i]] != key[i])
                                                        return;
                                                f(row);
                                            });
    }
    template<typename F>
    void for_each(const F& f) const
    {
        for (uint_t position = 0; position < m_rows.size(); ++position)
            f(m_rows[position]);
    }
    [[nodiscard]] size_t memory_usage() const noexcept
    {
        size_t result = m_rows.memory_usage();
        for (const auto& index : m_indexes)
            result += index.memory_usage();
        return result;
    }
};
}  // namespace ygg::database::datalog::detail

#endif
