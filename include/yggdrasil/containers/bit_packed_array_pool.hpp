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

#ifndef YGG_CONTAINERS_BIT_PACKED_ARRAY_POOL_HPP_
#define YGG_CONTAINERS_BIT_PACKED_ARRAY_POOL_HPP_

#include "yggdrasil/core/bit.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace ygg
{

template<std::unsigned_integral Block, bit::BlockCoder<Block> Coder, size_t FirstSegmentSize>
class BitPackedArrayPool;

/**
 * BasicBitPackedArrayView
 */

template<typename Block, typename Coder>
class BasicBitPackedArrayView
{
public:
    /**
     * Type aliases
     */

    using block_type = std::remove_const_t<Block>;
    using value_type = typename Coder::value_type;
    using reference_type = typename bit::int_reference<block_type, Coder>;
    using reference = std::conditional_t<std::is_const_v<Block>, value_type, reference_type>;

    /**
     * Compile-time properties
     */

    static constexpr std::size_t digits = std::numeric_limits<block_type>::digits;
    static constexpr size_t block_shift = std::countr_zero(digits);

private:
    template<typename, typename>
    friend class BasicBitPackedArrayView;

    template<std::unsigned_integral OtherBlock, bit::BlockCoder<OtherBlock> OtherCoder, size_t FirstSegmentSize>
    friend class BitPackedArrayPool;

    struct UncheckedTag
    {
    };

    static size_t checked_length(size_t length, uint8_t width, uint8_t offset)
    {
        validate_width(width);
        if (offset >= digits)
            throw std::invalid_argument("BasicBitPackedArrayView: offset must be below the block width.");
        if (length > static_cast<size_t>(std::numeric_limits<std::ptrdiff_t>::max()) || length > (std::numeric_limits<size_t>::max() - offset) / width)
            throw std::overflow_error("BasicBitPackedArrayView: length is too large.");
        return length;
    }

    static void validate_width(uint8_t width)
    {
        if (width == 0 || width > digits)
            throw std::invalid_argument("BasicBitPackedArrayView: width must be "
                                        "between 1 and the block width.");
    }

    void ensure_storage() const
    {
        if (m_data == nullptr && m_length > 0)
            throw std::logic_error("BasicBitPackedArrayView: invalid view.");
    }

    void ensure_index(size_t pos) const
    {
        ensure_storage();
        if (pos >= m_length)
            throw std::out_of_range("BasicBitPackedArrayView: index out of range.");
    }

    void ensure_not_empty() const
    {
        ensure_storage();
        if (empty())
            throw std::out_of_range("BasicBitPackedArrayView: view is empty.");
    }

    void ensure_fits(std::span<const value_type> elements) const
    {
        ensure_storage();
        if (elements.size() != m_length)
            throw std::invalid_argument("BasicBitPackedArrayView: wrong number of elements.");
    }

    BasicBitPackedArrayView(Block* data, size_t length, uint8_t width, uint8_t offset, UncheckedTag) noexcept :
        m_data(data),
        m_length(length),
        m_width(width),
        m_offset(offset)
    {
        assert(width > 0 && width <= digits);
        assert(offset < digits);
    }

public:
    /**
     * Iterator declarations
     */

    template<typename View>
    class BasicIterator;

    using iterator = BasicIterator<BasicBitPackedArrayView<Block, Coder>>;
    using const_iterator = BasicIterator<const BasicBitPackedArrayView<Block, Coder>>;

    /**
     * Constructors
     */

    BasicBitPackedArrayView(Block* data, size_t length, uint8_t width, uint8_t offset) :
        BasicBitPackedArrayView(data, checked_length(length, width, offset), width, offset, UncheckedTag {})
    {
    }

    template<typename OtherBlock>
        requires(std::is_const_v<Block> && !std::is_const_v<OtherBlock> && std::same_as<std::remove_const_t<OtherBlock>, block_type>)
    BasicBitPackedArrayView(const BasicBitPackedArrayView<OtherBlock, Coder>& other) noexcept :
        m_data(other.m_data),
        m_length(other.m_length),
        m_width(other.m_width),
        m_offset(other.m_offset)
    {
    }

    BasicBitPackedArrayView& operator=(std::span<const value_type> elements)
        requires(!std::is_const_v<Block>)
    {
        ensure_fits(elements);

        auto out = begin();
        for (const auto& element : elements)
            *out++ = element;

        return *this;
    }

    /**
     * Operators
     */

    friend bool operator==(const BasicBitPackedArrayView& lhs, const BasicBitPackedArrayView& rhs) { return std::ranges::equal(lhs, rhs); }

    friend bool operator==(const BasicBitPackedArrayView& lhs, std::span<const value_type> rhs) { return std::ranges::equal(lhs, rhs); }

    friend bool operator==(std::span<const value_type> lhs, const BasicBitPackedArrayView& rhs) { return rhs == lhs; }

    /**
     * Iterator definitions
     */

    template<typename View>
    class BasicIterator
    {
    private:
        using view_type = View;
        static constexpr bool is_const_iterator = std::is_const_v<View> || std::is_const_v<Block>;
        using block_pointer = std::conditional_t<is_const_iterator, const block_type*, block_type*>;

        void advance(std::ptrdiff_t n) noexcept
        {
            if (n == 0)
                return;

            const auto quotient = n / static_cast<difference_type>(digits);
            const auto remainder = n % static_cast<difference_type>(digits);
            auto block_delta = quotient * m_width;
            auto offset = static_cast<difference_type>(m_offset) + remainder * m_width;
            block_delta += offset / static_cast<difference_type>(digits);
            offset %= static_cast<difference_type>(digits);
            if (offset < 0)
            {
                --block_delta;
                offset += digits;
            }

            m_word += static_cast<std::ptrdiff_t>(block_delta);
            m_offset = static_cast<uint8_t>(offset);
        }

    public:
        using difference_type = std::ptrdiff_t;
        using raw_view_type = std::remove_const_t<View>;
        using value_type = typename raw_view_type::value_type;
        using reference = std::conditional_t<is_const_iterator, value_type, typename raw_view_type::reference_type>;
        using iterator_category = std::random_access_iterator_tag;
        using iterator_concept = std::random_access_iterator_tag;

        BasicIterator() noexcept : m_pos(0), m_word(nullptr), m_offset(0), m_width(0) {}
        BasicIterator(view_type& view, size_t pos) noexcept : m_pos(pos), m_word(view.m_data), m_offset(view.m_offset), m_width(view.m_width)
        {
            if (pos == 0 || m_word == nullptr)
                return;

            const auto bit_offset = static_cast<size_t>(m_offset) + pos * m_width;
            m_word += bit_offset >> block_shift;
            m_offset = static_cast<uint8_t>(bit_offset & (digits - 1));
        }

        reference operator*() const
        {
            if constexpr (is_const_iterator)
                return Coder::decode(bit::read_int<block_type>(m_word, m_offset, m_width));
            else
                return reference_type(m_word, m_offset, m_width);
        }

        BasicIterator& operator++() noexcept
        {
            const auto next_offset = static_cast<size_t>(m_offset) + m_width;
            m_word += next_offset >> block_shift;
            m_offset = static_cast<uint8_t>(next_offset & (digits - 1));
            ++m_pos;
            return *this;
        }

        BasicIterator operator++(int) noexcept
        {
            auto tmp = *this;
            ++(*this);
            return tmp;
        }

        BasicIterator& operator--() noexcept
        {
            if (m_offset >= m_width)
                m_offset -= m_width;
            else
            {
                --m_word;
                m_offset = static_cast<uint8_t>(digits - (m_width - m_offset));
            }
            --m_pos;
            return *this;
        }

        BasicIterator operator--(int) noexcept
        {
            auto tmp = *this;
            --(*this);
            return tmp;
        }

        BasicIterator& operator+=(difference_type n) noexcept
        {
            advance(n);
            m_pos += n;
            return *this;
        }

        BasicIterator& operator-=(difference_type n) noexcept
        {
            advance(-n);
            m_pos -= n;
            return *this;
        }

        friend BasicIterator operator+(BasicIterator it, difference_type n) noexcept
        {
            it += n;
            return it;
        }

        friend BasicIterator operator+(difference_type n, BasicIterator it) noexcept
        {
            it += n;
            return it;
        }

        friend BasicIterator operator-(BasicIterator it, difference_type n) noexcept
        {
            it -= n;
            return it;
        }

        friend difference_type operator-(const BasicIterator& lhs, const BasicIterator& rhs) noexcept
        {
            return static_cast<difference_type>(lhs.m_pos) - static_cast<difference_type>(rhs.m_pos);
        }

        reference operator[](difference_type n) const { return *(*this + n); }

        friend bool operator==(const BasicIterator&, const BasicIterator&) = default;
        friend auto operator<=>(const BasicIterator&, const BasicIterator&) = default;

    private:
        // Position first makes default comparisons short-circuit efficiently for unequal iterators.
        size_t m_pos;
        block_pointer m_word;
        uint8_t m_offset;
        uint8_t m_width;
    };

    /**
     * Accessors
     */

    reference_type operator[](size_t pos) noexcept
        requires(!std::is_const_v<Block>)
    {
        assert(pos < m_length);

        const size_t bit_index = static_cast<size_t>(m_offset) + pos * m_width;
        auto* word = m_data + (bit_index >> block_shift);
        const uint8_t offset = static_cast<uint8_t>(bit_index & (digits - 1));

        return reference_type(word, offset, m_width);
    }

    value_type operator[](size_t pos) const noexcept
    {
        assert(pos < m_length);

        const size_t bit_index = static_cast<size_t>(m_offset) + pos * m_width;
        const auto* word = m_data + (bit_index >> block_shift);
        const uint8_t offset = static_cast<uint8_t>(bit_index & (digits - 1));

        return Coder::decode(bit::read_int<block_type>(word, offset, m_width));
    }

    reference_type at(size_t pos)
        requires(!std::is_const_v<Block>)
    {
        ensure_index(pos);
        return (*this)[pos];
    }

    value_type at(size_t pos) const
    {
        ensure_index(pos);
        return (*this)[pos];
    }

    reference_type front()
        requires(!std::is_const_v<Block>)
    {
        ensure_not_empty();
        return (*this)[0];
    }

    value_type front() const
    {
        ensure_not_empty();
        return (*this)[0];
    }

    reference_type back()
        requires(!std::is_const_v<Block>)
    {
        ensure_not_empty();
        return (*this)[size() - 1];
    }

    value_type back() const
    {
        ensure_not_empty();
        return (*this)[size() - 1];
    }

    /**
     * Iterators
     */

    iterator begin() noexcept
        requires(!std::is_const_v<Block>)
    {
        return iterator(*this, 0);
    }
    iterator end() noexcept
        requires(!std::is_const_v<Block>)
    {
        return iterator(*this, size());
    }
    const_iterator begin() const noexcept { return const_iterator(*this, 0); }
    const_iterator end() const noexcept { return const_iterator(*this, size()); }
    const_iterator cbegin() const noexcept { return const_iterator(*this, 0); }
    const_iterator cend() const noexcept { return const_iterator(*this, size()); }

    /**
     * Capacity
     */

    size_t size() const noexcept { return m_length; }
    bool empty() const noexcept { return m_length == 0; }
    uint8_t width() const noexcept { return m_width; }

private:
    Block* m_data;
    size_t m_length;
    uint8_t m_width;
    uint8_t m_offset;
};

/// Stores fixed-length arrays as bit-packed unsigned integer codes with stable
/// references. Values are encoded and decoded via Coder.
template<std::unsigned_integral Block, bit::BlockCoder<Block> Coder = bit::ForwardingBlockCoder<Block>, size_t FirstSegmentSize = 16>
class BitPackedArrayPool
{
    static_assert(bit::is_power_of_two(FirstSegmentSize));

public:
    /**
     * Type aliases
     */

    using block_type = std::remove_const_t<Block>;
    using value_type = typename Coder::value_type;
    using reference_type = typename bit::int_reference<Block, Coder>;
    using ArrayView = BasicBitPackedArrayView<Block, Coder>;
    using ConstArrayView = BasicBitPackedArrayView<const Block, Coder>;

    /**
     * Compile-time properties
     */

private:
    /**
     * Type aliases
     */

    /**
     * Compile-time properties
     */

    static constexpr std::size_t digits = std::numeric_limits<block_type>::digits;
    static constexpr size_t block_shift = std::countr_zero(digits);

    static constexpr size_t seg_shift = std::countr_zero(FirstSegmentSize);
    static constexpr size_t seg_mask = FirstSegmentSize - 1;

    static size_t get_segment_index(size_t index) noexcept { return std::bit_width((index >> seg_shift) + 1) - 1; }
    static size_t get_segment_pos(size_t index, size_t seg_idx) noexcept
    {
        const size_t q = index >> seg_shift;
        const size_t r = index & seg_mask;
        return ((q - ((size_t { 1 } << seg_idx) - 1)) << seg_shift) + r;
    }

    static constexpr size_t blocks_for_bits(size_t bits) noexcept { return bit::ceil_div(bits, digits); }

    void reserve(size_t size)
    {
        if (size == 0 || size <= m_capacity)
            return;

        const size_t last_segment = get_segment_index(size - 1);
        const size_t first_new_segment = m_segments.size();

        m_segments.reserve(last_segment + 1);

        for (size_t seg = first_new_segment; seg <= last_segment; ++seg)
        {
            if (seg >= std::numeric_limits<size_t>::digits - seg_shift)
                throw std::length_error("BitPackedArrayPool: segment is too large.");
            const size_t arrays_in_segment = FirstSegmentSize << seg;  // geometric growth
            assert(bit::is_power_of_two(arrays_in_segment));

            if (arrays_in_segment > std::numeric_limits<size_t>::max() - m_capacity
                || (m_bits_per_array > 0 && arrays_in_segment > std::numeric_limits<size_t>::max() / m_bits_per_array))
                throw std::length_error("BitPackedArrayPool: segment is too large.");
            const size_t bits_in_segment = arrays_in_segment * m_bits_per_array;
            const size_t blocks_in_segment = blocks_for_bits(bits_in_segment);

            m_segments.emplace_back(blocks_in_segment, Block { 0 });
            m_capacity += arrays_in_segment;
        }
    }

    void ensure_index(size_t index) const
    {
        if (index >= m_size)
            throw std::out_of_range("BitPackedArrayPool: index out of range.");
    }

    void ensure_fits(std::span<const value_type> elements) const
    {
        if (elements.size() != m_length)
            throw std::invalid_argument("BitPackedArrayPool: wrong number of elements.");
    }

    ArrayView get_view(size_t index) noexcept
    {
        const size_t seg_idx = get_segment_index(index);
        const size_t seg_pos = get_segment_pos(index, seg_idx);
        const size_t start_bit = seg_pos * m_bits_per_array;

        auto* data = m_segments[seg_idx].data();
        if (const auto block_offset = start_bit >> block_shift; block_offset > 0)
            data += block_offset;
        const uint8_t offset = static_cast<uint8_t>(start_bit & (digits - 1));

        return ArrayView(data, m_length, m_width, offset, typename ArrayView::UncheckedTag {});
    }

    ConstArrayView get_view(size_t index) const noexcept
    {
        const size_t seg_idx = get_segment_index(index);
        const size_t seg_pos = get_segment_pos(index, seg_idx);
        const size_t start_bit = seg_pos * m_bits_per_array;

        const auto* data = m_segments[seg_idx].data();
        if (const auto block_offset = start_bit >> block_shift; block_offset > 0)
            data += block_offset;
        const uint8_t offset = static_cast<uint8_t>(start_bit & (digits - 1));

        return ConstArrayView(data, m_length, m_width, offset, typename ConstArrayView::UncheckedTag {});
    }

public:
    /**
     * Iterator declarations
     */

    template<typename Pool>
    class BasicIterator;

    using iterator = BasicIterator<BitPackedArrayPool>;
    using const_iterator = BasicIterator<const BitPackedArrayPool>;

    /**
     * Constructors
     */

    explicit BitPackedArrayPool(size_t length, uint8_t width) :
        m_bits_per_array(ArrayView::checked_length(length, width, 0) * width),
        m_length(length),
        m_width(width),
        m_capacity(0),
        m_size(0)
    {
    }

    /**
     * Iterator definitions
     */

    template<typename Pool>
    class BasicIterator
    {
    private:
        using pool_type = Pool;
        using pool_pointer = Pool*;

    public:
        using difference_type = std::ptrdiff_t;
        using value_type = ConstArrayView;
        using reference = std::conditional_t<std::is_const_v<pool_type>, ConstArrayView, ArrayView>;
        using iterator_category = std::random_access_iterator_tag;
        using iterator_concept = std::random_access_iterator_tag;

        BasicIterator() noexcept : m_pos(0), m_pool(nullptr) {}
        BasicIterator(pool_type& pool, size_t pos) noexcept : m_pos(pos), m_pool(&pool) {}

        reference operator*() const { return (*m_pool)[m_pos]; }

        BasicIterator& operator++() noexcept
        {
            ++m_pos;
            return *this;
        }

        BasicIterator operator++(int) noexcept
        {
            auto tmp = *this;
            ++(*this);
            return tmp;
        }

        BasicIterator& operator--() noexcept
        {
            --m_pos;
            return *this;
        }

        BasicIterator operator--(int) noexcept
        {
            auto tmp = *this;
            --(*this);
            return tmp;
        }

        BasicIterator& operator+=(difference_type n) noexcept
        {
            m_pos += n;
            return *this;
        }

        BasicIterator& operator-=(difference_type n) noexcept
        {
            m_pos -= n;
            return *this;
        }

        friend BasicIterator operator+(BasicIterator it, difference_type n) noexcept
        {
            it += n;
            return it;
        }

        friend BasicIterator operator+(difference_type n, BasicIterator it) noexcept
        {
            it += n;
            return it;
        }

        friend BasicIterator operator-(BasicIterator it, difference_type n) noexcept
        {
            it -= n;
            return it;
        }

        friend difference_type operator-(const BasicIterator& lhs, const BasicIterator& rhs) noexcept
        {
            return static_cast<difference_type>(lhs.m_pos) - static_cast<difference_type>(rhs.m_pos);
        }

        reference operator[](difference_type n) const { return *(*this + n); }

        friend bool operator==(const BasicIterator&, const BasicIterator&) = default;
        friend auto operator<=>(const BasicIterator&, const BasicIterator&) = default;

    private:
        size_t m_pos;
        pool_pointer m_pool;
    };

    /**
     * Accessors
     */

    ArrayView operator[](size_t index) noexcept
    {
        assert(index < m_size);
        return get_view(index);
    }

    ConstArrayView operator[](size_t index) const noexcept
    {
        assert(index < m_size);
        return get_view(index);
    }

    ArrayView at(size_t index)
    {
        ensure_index(index);
        return (*this)[index];
    }

    ConstArrayView at(size_t index) const
    {
        ensure_index(index);
        return (*this)[index];
    }

    /**
     * Iterators
     */

    iterator begin() noexcept { return iterator(*this, 0); }

    iterator end() noexcept { return iterator(*this, size()); }

    const_iterator begin() const noexcept { return const_iterator(*this, 0); }

    const_iterator end() const noexcept { return const_iterator(*this, size()); }

    const_iterator cbegin() const noexcept { return const_iterator(*this, 0); }

    const_iterator cend() const noexcept { return const_iterator(*this, size()); }

    /**
     * Modifiers
     */

    size_t push_back(std::span<const value_type> elements)
    {
        ensure_fits(elements);
        if (m_size == std::numeric_limits<size_t>::max())
            throw std::length_error("BitPackedArrayPool: size is too large.");

        const size_t index = m_size;
        reserve(m_size + 1);
        auto out = get_view(index).begin();
        for (const auto& element : elements)
            *out++ = element;
        ++m_size;

        return index;
    }

    void clear() noexcept { m_size = 0; }

    /**
     * Capacity
     */

    size_t length() const noexcept { return m_length; }
    uint8_t width() const noexcept { return m_width; }
    size_t capacity() const noexcept { return m_capacity; }
    size_t size() const noexcept { return m_size; }
    bool empty() const noexcept { return m_size == 0; }
    const auto& segments() const noexcept { return m_segments; }

    size_t memory_usage() const noexcept
    {
        size_t bytes = 0;
        for (const auto& segment : m_segments)
            bytes += segment.capacity() * sizeof(block_type);
        return bytes;
    }

private:
    // Segments grow geometrically, i.e., FirstSegmentSize, 2*FirstSegmentSize,
    // 4*FirstSegmentSize, ...
    std::vector<std::vector<block_type>> m_segments;

    size_t m_bits_per_array;
    size_t m_length;
    uint8_t m_width;

    size_t m_capacity;
    size_t m_size;
};
}  // namespace ygg

namespace std::ranges
{
template<typename Block, typename Coder>
inline constexpr bool enable_borrowed_range<::ygg::BasicBitPackedArrayView<Block, Coder>> = true;
}

#endif
