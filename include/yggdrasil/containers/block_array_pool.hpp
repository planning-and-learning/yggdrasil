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

#ifndef YGG_CONTAINERS_BLOCK_ARRAY_POOL_HPP_
#define YGG_CONTAINERS_BLOCK_ARRAY_POOL_HPP_

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
class BlockArrayPool;

/**
 * BasicBlockArrayView
 */

template<typename Block, typename Coder>
class BasicBlockArrayView
{
public:
    using block_type = std::remove_const_t<Block>;
    using value_type = typename Coder::value_type;
    using reference_type = typename bit::block_reference<block_type, Coder>;
    using reference = std::conditional_t<std::is_const_v<Block>, value_type, reference_type>;

private:
    template<typename, typename>
    friend class BasicBlockArrayView;

    template<std::unsigned_integral OtherBlock, bit::BlockCoder<OtherBlock> OtherCoder, size_t FirstSegmentSize>
    friend class BlockArrayPool;

    struct UncheckedTag
    {
    };

    static size_t checked_length(size_t length)
    {
        if (length > static_cast<size_t>(std::numeric_limits<std::ptrdiff_t>::max()))
            throw std::overflow_error("BasicBlockArrayView: length is too large.");
        return length;
    }

    BasicBlockArrayView(Block* data, size_t length, UncheckedTag) noexcept : m_data(data), m_length(length) {}

    void ensure_storage() const
    {
        if (m_data == nullptr && m_length > 0)
            throw std::logic_error("BasicBlockArrayView: invalid view.");
    }

    void ensure_index(size_t pos) const
    {
        ensure_storage();
        if (pos >= m_length)
            throw std::out_of_range("BasicBlockArrayView: index out of range.");
    }

    void ensure_not_empty() const
    {
        ensure_storage();
        if (empty())
            throw std::out_of_range("BasicBlockArrayView: view is empty.");
    }

    void ensure_fits(std::span<const value_type> elements) const
    {
        ensure_storage();
        if (elements.size() != m_length)
            throw std::invalid_argument("BasicBlockArrayView: wrong number of elements.");
    }

public:
    template<typename View>
    class BasicIterator;

    using iterator = BasicIterator<BasicBlockArrayView<Block, Coder>>;
    using const_iterator = BasicIterator<const BasicBlockArrayView<Block, Coder>>;

    BasicBlockArrayView(Block* data, size_t length) : BasicBlockArrayView(data, checked_length(length), UncheckedTag {}) {}

    template<typename OtherBlock>
        requires(std::is_const_v<Block> && !std::is_const_v<OtherBlock> && std::same_as<std::remove_const_t<OtherBlock>, block_type>)
    BasicBlockArrayView(const BasicBlockArrayView<OtherBlock, Coder>& other) noexcept : m_data(other.m_data), m_length(other.m_length)
    {
    }

    BasicBlockArrayView& operator=(std::span<const value_type> elements)
        requires(!std::is_const_v<Block>)
    {
        ensure_fits(elements);

        auto out = begin();
        for (const auto& element : elements)
            *out++ = element;

        return *this;
    }

    friend bool operator==(const BasicBlockArrayView& lhs, const BasicBlockArrayView& rhs) { return std::ranges::equal(lhs, rhs); }

    friend bool operator==(const BasicBlockArrayView& lhs, std::span<const value_type> rhs) { return std::ranges::equal(lhs, rhs); }

    friend bool operator==(std::span<const value_type> lhs, const BasicBlockArrayView& rhs) { return rhs == lhs; }

    template<typename View>
    class BasicIterator
    {
    private:
        using view_type = View;
        static constexpr bool is_const_iterator = std::is_const_v<View> || std::is_const_v<Block>;
        using block_pointer = std::conditional_t<is_const_iterator, const block_type*, block_type*>;

    public:
        using difference_type = std::ptrdiff_t;
        using raw_view_type = std::remove_const_t<View>;
        using value_type = typename raw_view_type::value_type;
        using reference = std::conditional_t<is_const_iterator, value_type, typename raw_view_type::reference_type>;
        using iterator_category = std::random_access_iterator_tag;
        using iterator_concept = std::random_access_iterator_tag;

        BasicIterator() noexcept : m_pos(0), m_data(nullptr) {}
        BasicIterator(view_type& view, size_t pos) noexcept : m_pos(pos), m_data(view.m_data) {}

        reference operator*() const
        {
            if constexpr (is_const_iterator)
                return Coder::decode(m_data[m_pos]);
            else
                return reference_type(m_data + m_pos);
        }

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
        // Position first makes default comparisons short-circuit efficiently for unequal iterators.
        size_t m_pos;
        block_pointer m_data;
    };

    reference_type operator[](size_t pos) noexcept
        requires(!std::is_const_v<Block>)
    {
        assert(pos < m_length);
        return reference_type(m_data + pos);
    }

    value_type operator[](size_t pos) const noexcept
    {
        assert(pos < m_length);
        return Coder::decode(m_data[pos]);
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

    size_t size() const noexcept { return m_length; }
    bool empty() const noexcept { return m_length == 0; }

private:
    Block* m_data;
    size_t m_length;
};

/// Stores fixed-length arrays where each element occupies one full Block.
/// Values are encoded and decoded via Coder.
template<std::unsigned_integral Block, bit::BlockCoder<Block> Coder = bit::ForwardingBlockCoder<Block>, size_t FirstSegmentSize = 16>
class BlockArrayPool
{
    static_assert(bit::is_power_of_two(FirstSegmentSize));

public:
    using block_type = std::remove_const_t<Block>;
    using value_type = typename Coder::value_type;
    using ArrayView = BasicBlockArrayView<Block, Coder>;
    using ConstArrayView = BasicBlockArrayView<const Block, Coder>;

private:
    static constexpr size_t seg_shift = std::countr_zero(FirstSegmentSize);
    static constexpr size_t seg_mask = FirstSegmentSize - 1;

    static size_t get_segment_index(size_t index) noexcept { return std::bit_width((index >> seg_shift) + 1) - 1; }

    static size_t get_segment_pos(size_t index, size_t seg_idx) noexcept
    {
        const size_t q = index >> seg_shift;
        const size_t r = index & seg_mask;
        return ((q - ((size_t { 1 } << seg_idx) - 1)) << seg_shift) + r;
    }

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
                throw std::length_error("BlockArrayPool: segment is too large.");
            const size_t arrays_in_segment = FirstSegmentSize << seg;
            assert(bit::is_power_of_two(arrays_in_segment));

            if (arrays_in_segment > std::numeric_limits<size_t>::max() - m_capacity
                || (m_length > 0 && arrays_in_segment > std::numeric_limits<size_t>::max() / m_length))
                throw std::length_error("BlockArrayPool: segment is too large.");
            const size_t blocks_in_segment = arrays_in_segment * m_length;
            m_segments.emplace_back(blocks_in_segment, Block { 0 });
            m_capacity += arrays_in_segment;
        }
    }

    void ensure_index(size_t index) const
    {
        if (index >= m_size)
            throw std::out_of_range("BlockArrayPool: index out of range.");
    }

    void ensure_fits(std::span<const value_type> elements) const
    {
        if (elements.size() != m_length)
            throw std::invalid_argument("BlockArrayPool: wrong number of elements.");
    }

    ArrayView get_view(size_t index) noexcept
    {
        const size_t seg_idx = get_segment_index(index);
        const size_t seg_pos = get_segment_pos(index, seg_idx);
        auto* data = m_segments[seg_idx].data();
        if (const auto block_offset = seg_pos * static_cast<size_t>(m_length); block_offset > 0)
            data += block_offset;

        return ArrayView(data, m_length, typename ArrayView::UncheckedTag {});
    }

    ConstArrayView get_view(size_t index) const noexcept
    {
        const size_t seg_idx = get_segment_index(index);
        const size_t seg_pos = get_segment_pos(index, seg_idx);
        const auto* data = m_segments[seg_idx].data();
        if (const auto block_offset = seg_pos * static_cast<size_t>(m_length); block_offset > 0)
            data += block_offset;

        return ConstArrayView(data, m_length, typename ConstArrayView::UncheckedTag {});
    }

public:
    template<typename Pool>
    class BasicIterator;

    using iterator = BasicIterator<BlockArrayPool>;
    using const_iterator = BasicIterator<const BlockArrayPool>;

    explicit BlockArrayPool(size_t length) : m_length(ArrayView::checked_length(length)), m_capacity(0), m_size(0) {}

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

    iterator begin() noexcept { return iterator(*this, 0); }
    iterator end() noexcept { return iterator(*this, size()); }
    const_iterator begin() const noexcept { return const_iterator(*this, 0); }
    const_iterator end() const noexcept { return const_iterator(*this, size()); }
    const_iterator cbegin() const noexcept { return const_iterator(*this, 0); }
    const_iterator cend() const noexcept { return const_iterator(*this, size()); }

    size_t push_back(std::span<const value_type> elements)
    {
        ensure_fits(elements);
        if (m_size == std::numeric_limits<size_t>::max())
            throw std::length_error("BlockArrayPool: size is too large.");

        const size_t index = m_size;
        reserve(m_size + 1);
        auto out = get_view(index).begin();
        for (const auto& element : elements)
            *out++ = element;
        ++m_size;

        return index;
    }

    void clear() noexcept { m_size = 0; }

    size_t length() const noexcept { return m_length; }
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
    std::vector<std::vector<block_type>> m_segments;

    size_t m_length;
    size_t m_capacity;
    size_t m_size;
};

}  // namespace ygg

namespace std::ranges
{
template<typename Block, typename Coder>
inline constexpr bool enable_borrowed_range<::ygg::BasicBlockArrayView<Block, Coder>> = true;
}

#endif
