/*
 * Copyright (C) 2025-2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef YGG_CONTAINERS_SPARSE_BITSET_HPP_
#define YGG_CONTAINERS_SPARSE_BITSET_HPP_

#include "yggdrasil/containers/paged_vector.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

namespace ygg
{

/// Set of indices from a large, sparsely used index space. Words are stored in pages allocated on first access, and
/// the touched words are recorded, so reset() and iteration cost O(touched words) rather than O(index space).
/// Iteration visits words in the order they were first touched and the bits of a word in ascending order.
/// Concurrent reads are safe in the absence of concurrent writes.
template<size_t PageBits = 6>
class SparseBitset
{
private:
    using Word = std::uint64_t;
    static constexpr size_t kWordBits = 64;

public:
    /// Returns whether i was absent.
    bool insert(size_t i)
    {
        const auto word_index = i / kWordBits;
        auto& word = m_words.get_or_create(word_index);
        const auto mask = Word(1) << (i % kWordBits);
        if (word & mask)
            return false;
        if (word == 0)
            m_touched.push_back(word_index);
        word |= mask;
        return true;
    }

    bool contains(size_t i) const noexcept
    {
        const auto* word = m_words.find(i / kWordBits);
        return word && (*word >> (i % kWordBits)) & 1;
    }

    void reset() noexcept
    {
        for (const auto word_index : m_touched)
            *m_words.find(word_index) = 0;
        m_touched.clear();
    }

    bool empty() const noexcept { return m_touched.empty(); }

    class const_iterator
    {
    public:
        using value_type = size_t;
        using reference = size_t;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::forward_iterator_tag;
        using iterator_concept = std::forward_iterator_tag;

        const_iterator() noexcept = default;
        const_iterator(const SparseBitset& set, size_t touched) noexcept : m_set(&set), m_touched(touched), m_remaining(0) { load(); }

        size_t operator*() const noexcept { return m_set->m_touched[m_touched] * kWordBits + size_t(std::countr_zero(m_remaining)); }

        const_iterator& operator++() noexcept
        {
            m_remaining &= m_remaining - 1;
            if (m_remaining == 0)
            {
                ++m_touched;
                load();
            }
            return *this;
        }

        const_iterator operator++(int) noexcept
        {
            auto copy = *this;
            ++(*this);
            return copy;
        }

        friend bool operator==(const const_iterator& lhs, const const_iterator& rhs) noexcept
        {
            return lhs.m_touched == rhs.m_touched && lhs.m_remaining == rhs.m_remaining;
        }

    private:
        void load() noexcept
        {
            // Words emptied since being touched do not occur: reset() clears the touched list.
            m_remaining = m_touched < m_set->m_touched.size() ? *m_set->m_words.find(m_set->m_touched[m_touched]) : 0;
        }

        const SparseBitset* m_set = nullptr;
        size_t m_touched = 0;
        Word m_remaining = 0;
    };

    const_iterator begin() const noexcept { return const_iterator(*this, 0); }
    const_iterator end() const noexcept { return const_iterator(*this, m_touched.size()); }

    size_t memory_usage() const noexcept { return m_words.memory_usage() + m_touched.capacity() * sizeof(size_t); }

private:
    PagedVector<Word, PageBits> m_words;
    std::vector<size_t> m_touched;
};

}  // namespace ygg

#endif
