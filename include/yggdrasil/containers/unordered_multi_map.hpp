/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_CONTAINERS_UNORDERED_MULTI_MAP_HPP_
#define YGG_CONTAINERS_UNORDERED_MULTI_MAP_HPP_

#include "yggdrasil/containers/associative_containers.hpp"
#include "yggdrasil/semantics/equal_to.hpp"
#include "yggdrasil/semantics/hash.hpp"

#include <cstddef>
#include <iterator>
#include <limits>
#include <memory>
#include <ranges>
#include <utility>
#include <vector>

namespace ygg
{

/// Append-and-clear multimap: one flat hash-table entry per distinct key,
/// with values and their next indices stored together in a contiguous vector.
/// Duplicate key/value pairs are retained. Value order is unspecified.
/// clear() and reserve() retain capacity; values may own additional storage.
/// Mutations invalidate all returned ranges, iterators, and references.
template<typename Key, typename Value>
class UnorderedMultiMap
{
private:
    static constexpr size_t npos = std::numeric_limits<size_t>::max();

    struct Entry
    {
        Value value;
        size_t next;
    };

    UnorderedMap<Key, size_t> m_heads;
    std::vector<Entry> m_entries;

public:
    class ValueIterator
    {
    private:
        const Entry* m_entries = nullptr;
        size_t m_index = npos;

        friend class UnorderedMultiMap;
        ValueIterator(const Entry* entries, size_t index) noexcept : m_entries(entries), m_index(index) {}

    public:
        using value_type = Value;
        using difference_type = std::ptrdiff_t;
        using reference = const Value&;
        using pointer = const Value*;
        using iterator_category = std::forward_iterator_tag;

        ValueIterator() = default;
        reference operator*() const noexcept { return m_entries[m_index].value; }
        pointer operator->() const noexcept { return std::addressof(operator*()); }
        ValueIterator& operator++() noexcept
        {
            m_index = m_entries[m_index].next;
            return *this;
        }
        ValueIterator operator++(int) noexcept
        {
            auto previous = *this;
            ++*this;
            return previous;
        }
        friend bool operator==(const ValueIterator&, const ValueIterator&) = default;
    };

    UnorderedMultiMap() = default;
    UnorderedMultiMap(const UnorderedMultiMap&) = delete;
    UnorderedMultiMap& operator=(const UnorderedMultiMap&) = delete;
    UnorderedMultiMap(UnorderedMultiMap&&) = default;
    UnorderedMultiMap& operator=(UnorderedMultiMap&&) = default;

    void insert(const Key& key, Value value)
    {
        const auto [head, inserted] = m_heads.try_emplace(key, npos);
        try
        {
            m_entries.push_back({ std::move(value), head->second });
        }
        catch (...)
        {
            if (inserted)
                m_heads.erase(head);
            throw;
        }
        head->second = m_entries.size() - 1;
    }

    /// Read-only range of values associated with key; empty for a missing key.
    auto values(const Key& key) const
    {
        const auto head = m_heads.find(key);
        return std::ranges::subrange(ValueIterator(m_entries.data(), head == m_heads.end() ? npos : head->second), ValueIterator(m_entries.data(), npos));
    }

    void clear() noexcept
    {
        m_heads.clear();
        m_entries.clear();
    }

    /// Reserve for at most count values and count distinct keys, without shrinking.
    void reserve(size_t count)
    {
        // GTL reserve(0) would release an empty hash table's retained storage.
        if (count != 0)
            m_heads.reserve(count);
        m_entries.reserve(count);
    }

    size_t size() const noexcept { return m_entries.size(); }
    bool empty() const noexcept { return m_entries.empty(); }
};

}  // namespace ygg

#endif
