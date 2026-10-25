/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_DATALOG_DETAILS_TUPLES_HPP_
#define YGG_DATABASE_DATALOG_DETAILS_TUPLES_HPP_

#include "yggdrasil/core/types.hpp"

#include <algorithm>
#include <cstddef>
#include <gtl/phmap.hpp>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

/// Row storage of the evaluator (evaluator.hpp): fixed-arity tuples of object ids, hashed with a fast
/// integer mix rather than a byte hash, since rows are a few words.
namespace ygg::database::datalog::detail
{
/// Hash of a short sequence of ids: a multiplicative mix per value and a final avalanche (splitmix64's finalizer),
/// with splitmix64's constants.
namespace hashing
{
inline constexpr uint64_t seed = 0x9E3779B97F4A7C15ULL;
inline constexpr uint64_t multiplier = 0xBF58476D1CE4E5B9ULL;
inline constexpr uint64_t avalanche = 0x94D049BB133111EBULL;
inline constexpr int first_shift = 31;
inline constexpr int second_shift = 29;
}  // namespace hashing

inline hash_t finish_hash(uint64_t hash) noexcept
{
    hash ^= hash >> hashing::first_shift;
    hash *= hashing::avalanche;
    return hash ^ (hash >> hashing::second_shift);
}
inline hash_t hash_values(std::span<const uint_t> values) noexcept
{
    uint64_t hash = hashing::seed ^ values.size();
    for (const auto value : values)
        hash = (hash ^ value) * hashing::multiplier;
    return finish_hash(hash);
}
/// hash_values of the row's values at these columns, without copying them.
inline hash_t hash_columns(std::span<const uint_t> row, const std::vector<size_t>& columns) noexcept
{
    uint64_t hash = hashing::seed ^ columns.size();
    for (const auto column : columns)
        hash = (hash ^ row[column]) * hashing::multiplier;
    return finish_hash(hash);
}

/// A set of tuples of one arity. Erase moves the last row into the erased position.
/// Not movable: the hash set's functors point into it.
class TupleSet
{
    struct RowHash
    {
        using is_transparent = void;
        const TupleSet* rows;
        size_t operator()(uint_t index) const noexcept { return hash_values(rows->data(index)); }
        size_t operator()(std::span<const uint_t> row) const noexcept { return hash_values(row); }
    };
    struct RowEqual
    {
        using is_transparent = void;
        const TupleSet* rows;
        static bool equal(std::span<const uint_t> lhs, std::span<const uint_t> rhs) noexcept { return std::ranges::equal(lhs, rhs); }
        bool operator()(uint_t lhs, uint_t rhs) const noexcept { return equal(rows->data(lhs), rows->data(rhs)); }
        bool operator()(std::span<const uint_t> lhs, uint_t rhs) const noexcept { return equal(lhs, rows->data(rhs)); }
        bool operator()(uint_t lhs, std::span<const uint_t> rhs) const noexcept { return equal(rows->data(lhs), rhs); }
    };

    size_t m_arity;
    size_t m_size = 0;
    std::vector<uint_t> m_values;
    gtl::flat_hash_set<uint_t, RowHash, RowEqual> m_rows;

    [[nodiscard]] std::span<const uint_t> data(uint_t index) const noexcept
    { return std::span(m_values).subspan(static_cast<size_t>(index) * m_arity, m_arity); }

public:
    explicit TupleSet(size_t arity) : m_arity(arity), m_rows(0, RowHash { this }, RowEqual { this }) {}
    ~TupleSet() = default;
    TupleSet(const TupleSet&) = delete;
    TupleSet& operator=(const TupleSet&) = delete;
    TupleSet(TupleSet&&) = delete;
    TupleSet& operator=(TupleSet&&) = delete;

    [[nodiscard]] size_t arity() const noexcept { return m_arity; }
    [[nodiscard]] size_t size() const noexcept { return m_size; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }
    std::span<const uint_t> operator[](uint_t index) const noexcept { return data(index); }

    [[nodiscard]] std::optional<uint_t> find(std::span<const uint_t> row) const
    {
        const auto it = m_rows.find(row);
        return it == m_rows.end() ? std::nullopt : std::optional(*it);
    }
    [[nodiscard]] bool contains(std::span<const uint_t> row) const { return m_rows.find(row) != m_rows.end(); }
    /// The row's position and whether it was inserted.
    std::pair<uint_t, bool> insert(std::span<const uint_t> row)
    {
        if (const auto it = m_rows.find(row); it != m_rows.end())
            return { *it, false };
        const auto index = to_uint_t(m_size);
        m_values.insert(m_values.end(), row.begin(), row.end());
        ++m_size;
        m_rows.insert(index);
        return { index, true };
    }
    /// Moves the last row into `index`.
    void erase(uint_t index)
    {
        const auto last = to_uint_t(m_size - 1);
        m_rows.erase(index);
        if (index != last)
        {
            m_rows.erase(last);
            std::ranges::copy(data(last), m_values.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(index) * m_arity));
            m_rows.insert(index);
        }
        m_values.resize(static_cast<size_t>(last) * m_arity);
        --m_size;
    }
    void clear() noexcept
    {
        m_rows.clear();
        m_values.clear();
        m_size = 0;
    }
    [[nodiscard]] size_t memory_usage() const noexcept { return m_values.capacity() * sizeof(uint_t) + m_rows.capacity() * (sizeof(uint_t) + 1); }
};

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
    [[nodiscard]] hash_t hash(std::span<const uint_t> row) const noexcept { return hash_columns(row, m_columns); }
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
    /// Removes the row at `position`, then moves the set's last row (`last`) into it, as TupleSet::erase does.
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
        const auto it = m_heads.find(hash_values(key));
        if (it == m_heads.end())
            return;
        for (auto position = it->second.first; position != none; position = m_next[position])
            f(position);
    }
    /// The number of rows whose key hashes like `key`'s values.
    [[nodiscard]] size_t count(std::span<const uint_t> key) const
    {
        const auto it = m_heads.find(hash_values(key));
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
    TupleSet m_rows;
    std::vector<TupleIndex> m_indexes;

public:
    explicit IndexedTuples(size_t arity) : m_rows(arity) {}

    [[nodiscard]] const TupleSet& rows() const noexcept { return m_rows; }
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
        const auto [position, inserted] = m_rows.insert(row);
        if (inserted)
            for (auto& index : m_indexes)
                index.insert(position, row);
        return inserted;
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
