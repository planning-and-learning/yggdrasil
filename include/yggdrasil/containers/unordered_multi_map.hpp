/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_CONTAINERS_UNORDERED_MULTI_MAP_HPP_
#define YGG_CONTAINERS_UNORDERED_MULTI_MAP_HPP_

#include "yggdrasil/containers/associative_containers.hpp"
#include "yggdrasil/semantics/equal_to.hpp"
#include "yggdrasil/semantics/hash.hpp"

#include <concepts>
#include <cstddef>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace ygg
{

/// Multimap with one flat hash-table entry per distinct key and contiguous
/// value slots. Erasure destroys the value and links its slot into a free list;
/// insertion reuses those slots before growing the vector.
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
        std::optional<Value> value;
        size_t next;
    };

    UnorderedMap<Key, size_t> m_heads;
    std::vector<Entry> m_entries;
    size_t m_free_head = npos;
    size_t m_size = 0;

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
        reference operator*() const noexcept { return *m_entries[m_index].value; }
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
    UnorderedMultiMap(UnorderedMultiMap&& other) noexcept(std::is_nothrow_move_constructible_v<UnorderedMap<Key, size_t>>) :
        m_heads(std::move(other.m_heads)),
        m_entries(std::move(other.m_entries)),
        m_free_head(std::exchange(other.m_free_head, npos)),
        m_size(std::exchange(other.m_size, 0))
    {
    }

    UnorderedMultiMap& operator=(UnorderedMultiMap&& other) noexcept(std::is_nothrow_move_assignable_v<UnorderedMap<Key, size_t>>)
    {
        if (this != &other)
        {
            m_heads = std::move(other.m_heads);
            m_entries = std::move(other.m_entries);
            m_free_head = std::exchange(other.m_free_head, npos);
            m_size = std::exchange(other.m_size, 0);
        }
        return *this;
    }

    void insert(const Key& key, Value value)
    {
        const auto [head, inserted] = m_heads.try_emplace(key, npos);
        try
        {
            if (m_free_head == npos)
            {
                m_entries.push_back({ std::move(value), head->second });
                head->second = m_entries.size() - 1;
            }
            else
            {
                auto& entry = m_entries[m_free_head];
                entry.value.emplace(std::move(value));
                const auto position = m_free_head;
                m_free_head = entry.next;
                entry.next = head->second;
                head->second = position;
            }
            ++m_size;
        }
        catch (...)
        {
            if (inserted)
                m_heads.erase(head);
            throw;
        }
    }

    /// Remove one matching pair and retain its slot. Other duplicates remain.
    bool erase(const Key& key, const Value& value)
        requires EqualityComparableByEqualTo<Value>
    {
        const auto head = m_heads.find(key);
        if (head == m_heads.end())
            return false;
        // ponytail: finding a value scans its key chain; add direct handles only
        // if profiling shows highly skewed keys dominate update time.
        auto* link = &head->second;
        while (*link != npos)
        {
            const auto position = *link;
            auto& entry = m_entries[position];
            if (EqualTo<Value> {}(*entry.value, value))
            {
                if (position == head->second && entry.next == npos)
                {
                    // Acquire deletion headroom before mutation so GTL can clean
                    // tombstones in place during subsequent bounded-size churn.
                    if (m_heads.size() > std::numeric_limits<size_t>::max() / 2)
                        throw std::length_error("UnorderedMultiMap: erase headroom exceeds addressable memory.");
                    m_heads.reserve(m_heads.size() * 2);
                    m_heads.erase(key);
                }
                else
                    *link = entry.next;
                entry.value.reset();
                entry.next = m_free_head;
                m_free_head = position;
                --m_size;
                return true;
            }
            link = &entry.next;
        }
        return false;
    }

    /// Replace one matching mapped value without changing its key or slot.
    bool replace(const Key& key, const Value& old_value, Value new_value)
        requires EqualityComparableByEqualTo<Value> && std::assignable_from<Value&, Value>
    {
        const auto head = m_heads.find(key);
        if (head == m_heads.end())
            return false;
        for (auto position = head->second; position != npos; position = m_entries[position].next)
            if (EqualTo<Value> {}(*m_entries[position].value, old_value))
            {
                *m_entries[position].value = std::move(new_value);
                return true;
            }
        return false;
    }

    /// Read-only range of values associated with key; empty for a missing key.
    auto values(const Key& key) const&
    {
        const auto head = m_heads.find(key);
        return std::ranges::subrange(ValueIterator(m_entries.data(), head == m_heads.end() ? npos : head->second), ValueIterator(m_entries.data(), npos));
    }

    auto values(const Key& key) const&& = delete;

    void clear() noexcept
    {
        m_heads.clear();
        m_entries.clear();
        m_free_head = npos;
        m_size = 0;
    }

    /// Reserve for at most count values and count distinct keys, without shrinking.
    void reserve(size_t count)
    {
        // GTL reserve(0) would release an empty hash table's retained storage.
        if (count != 0)
            m_heads.reserve(count);
        m_entries.reserve(count);
    }

    /// Retained entries and hash slots; excludes the container's fixed storage.
    size_t memory_usage() const noexcept
    {
        return m_entries.capacity() * sizeof(Entry) + m_heads.capacity() * (sizeof(std::pair<const Key, size_t>) + sizeof(gtl::priv::ctrl_t));
    }

    size_t size() const noexcept { return m_size; }
    bool empty() const noexcept { return m_size == 0; }
};

}  // namespace ygg

#endif
