/*
 * Copyright (C) 2026 Dominik Drexler
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

#ifndef YGG_CONTAINERS_SEGMENTED_BIT_VECTOR_HPP_
#define YGG_CONTAINERS_SEGMENTED_BIT_VECTOR_HPP_

#include "yggdrasil/containers/bit_packed_array_pool.hpp"
#include "yggdrasil/core/config.hpp"

#include <compare>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <limits>
#include <span>
#include <type_traits>

namespace ygg
{
/// A geometrically segmented, packed vector of bits with stable references.
/// ThreadSafe has the same publication and quiescence requirements as
/// BitPackedArrayPool.
template<std::unsigned_integral Block = uint_t, size_t FirstSegmentSize = std::numeric_limits<Block>::digits, bool ThreadSafe = false>
class SegmentedBitVector
{
private:
    struct BoolCoder
    {
        using value_type = bool;

        static constexpr bool decode(Block value) noexcept { return value != 0; }
        static constexpr Block encode(bool value) noexcept { return static_cast<Block>(value); }
    };

    using Pool = BitPackedArrayPool<Block, BoolCoder, FirstSegmentSize, ThreadSafe>;

public:
    using block_type = typename Pool::block_type;
    using value_type = bool;
    using reference = typename Pool::reference_type;
    using const_reference = bool;

    static constexpr bool thread_safe = ThreadSafe;

    template<typename Vector>
    class BasicIterator
    {
    private:
        using vector_type = Vector;
        using vector_pointer = Vector*;

    public:
        using difference_type = std::ptrdiff_t;
        using value_type = bool;
        using reference = std::conditional_t<std::is_const_v<vector_type>, bool, typename SegmentedBitVector::reference>;
        using iterator_category = std::random_access_iterator_tag;
        using iterator_concept = std::random_access_iterator_tag;

        BasicIterator() noexcept : m_pos(0), m_vector(nullptr) {}
        BasicIterator(vector_type& vector, size_t pos) noexcept : m_pos(pos), m_vector(&vector) {}

        reference operator*() const { return (*m_vector)[m_pos]; }

        BasicIterator& operator++() noexcept
        {
            ++m_pos;
            return *this;
        }

        BasicIterator operator++(int) noexcept
        {
            auto copy = *this;
            ++(*this);
            return copy;
        }

        BasicIterator& operator--() noexcept
        {
            --m_pos;
            return *this;
        }

        BasicIterator operator--(int) noexcept
        {
            auto copy = *this;
            --(*this);
            return copy;
        }

        BasicIterator& operator+=(difference_type offset) noexcept
        {
            m_pos += offset;
            return *this;
        }

        BasicIterator& operator-=(difference_type offset) noexcept
        {
            m_pos -= offset;
            return *this;
        }

        friend BasicIterator operator+(BasicIterator iterator, difference_type offset) noexcept
        {
            iterator += offset;
            return iterator;
        }

        friend BasicIterator operator+(difference_type offset, BasicIterator iterator) noexcept
        {
            iterator += offset;
            return iterator;
        }

        friend BasicIterator operator-(BasicIterator iterator, difference_type offset) noexcept
        {
            iterator -= offset;
            return iterator;
        }

        friend difference_type operator-(const BasicIterator& lhs, const BasicIterator& rhs) noexcept
        {
            return static_cast<difference_type>(lhs.m_pos) - static_cast<difference_type>(rhs.m_pos);
        }

        reference operator[](difference_type offset) const { return *(*this + offset); }

        friend bool operator==(const BasicIterator&, const BasicIterator&) = default;
        friend auto operator<=>(const BasicIterator&, const BasicIterator&) = default;

    private:
        size_t m_pos;
        vector_pointer m_vector;
    };

    using iterator = BasicIterator<SegmentedBitVector>;
    using const_iterator = BasicIterator<const SegmentedBitVector>;

    SegmentedBitVector() : m_pool(1, 1) {}

    reference operator[](size_t pos) noexcept { return m_pool[pos][0]; }
    bool operator[](size_t pos) const noexcept { return m_pool[pos][0]; }

    reference at(size_t pos) { return m_pool.at(pos)[0]; }
    bool at(size_t pos) const { return m_pool.at(pos)[0]; }

    reference front() { return at(0); }
    bool front() const { return at(0); }
    reference back() { return at(size() - 1); }
    bool back() const { return at(size() - 1); }

    iterator begin() noexcept { return iterator(*this, 0); }
    iterator end() noexcept { return iterator(*this, size()); }
    const_iterator begin() const noexcept { return const_iterator(*this, 0); }
    const_iterator end() const noexcept { return const_iterator(*this, size()); }
    const_iterator cbegin() const noexcept { return const_iterator(*this, 0); }
    const_iterator cend() const noexcept { return const_iterator(*this, size()); }

    void push_back(bool value) { m_pool.push_back(std::span<const bool>(&value, 1)); }
    void pop_back() { m_pool.pop_back(); }
    void clear() noexcept { m_pool.clear(); }
    void resize(size_t size, bool value = false) { m_pool.resize(size, std::span<const bool>(&value, 1)); }

    size_t size() const noexcept { return m_pool.size(); }
    size_t capacity() const noexcept { return m_pool.capacity(); }
    bool empty() const noexcept { return m_pool.empty(); }
    size_t memory_usage() const noexcept { return m_pool.memory_usage(); }

private:
    Pool m_pool;
};
}  // namespace ygg

#endif
