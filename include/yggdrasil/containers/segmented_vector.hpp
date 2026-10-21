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

#ifndef YGG_CONTAINERS_SEGMENTED_VECTOR_HPP_
#define YGG_CONTAINERS_SEGMENTED_VECTOR_HPP_

#include "yggdrasil/containers/detail/geometric_segment_layout.hpp"
#include "yggdrasil/containers/detail/threading.hpp"

#include <array>
#include <atomic>
#include <cassert>
#include <compare>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace ygg
{
/// ThreadSafe permits concurrent appends, resize growth, size queries, and reads
/// after publication was observed through size() or external synchronization.
/// Clear, resize shrink, pop_back, iteration, memory inspection, move,
/// destruction, and mutation of published elements require quiescence or
/// external locking.
/// Index bounds the number of addressable elements and hence the number of segment slots kept inline.
template<typename T, size_t FirstSegmentSize = 32, bool ThreadSafe = false, std::unsigned_integral Index = size_t>
class SegmentedVector
{
private:
    using Layout = detail::GeometricSegmentLayout<FirstSegmentSize, Index>;

    static_assert(Layout::max_segments > 0);

    T* segment(size_t index) const noexcept { return detail::load_pointer<ThreadSafe, T>(m_segments[index]); }

    T* element(size_t pos) const noexcept
    {
        assert(pos <= std::numeric_limits<Index>::max());
        const auto index = Layout::segment_index(static_cast<Index>(pos));
        return segment(index) + Layout::segment_offset(static_cast<Index>(pos), index);
    }

    void resize_to_fit(size_t n)
    {
        auto capacity = detail::load_size<ThreadSafe>(m_capacity);
        while (capacity < n)
        {
            const auto index = m_num_segments;
            if (index >= Layout::max_segments)
                throw std::length_error("SegmentedVector: segment is too large.");

            const auto new_segment_size = Layout::segment_capacity(index);
            if (new_segment_size > std::numeric_limits<size_t>::max() - capacity || new_segment_size > std::numeric_limits<size_t>::max() / sizeof(T))
                throw std::length_error("SegmentedVector: segment is too large.");

            auto* data = static_cast<T*>(::operator new(new_segment_size * sizeof(T), std::align_val_t { alignof(T) }));
            detail::store_pointer<ThreadSafe, T>(m_segments[index], data);
            m_num_segments = index + 1;
            capacity += new_segment_size;
            detail::store_size<ThreadSafe>(m_capacity, capacity);
        }
    }

    template<typename... Args>
    T& emplace_back_at_unlocked(size_t size, Args&&... args)
    {
        if (size == std::numeric_limits<size_t>::max())
            throw std::length_error("SegmentedVector: size is too large.");
        resize_to_fit(size + 1);

        auto* result = std::construct_at(element(size), std::forward<Args>(args)...);
        detail::store_size<ThreadSafe>(m_size, size + 1);
        return *result;
    }

    template<typename... Args>
    T& emplace_back_unlocked(Args&&... args)
    {
        return emplace_back_at_unlocked(detail::load_size<ThreadSafe>(m_size), std::forward<Args>(args)...);
    }

    size_t push_back_bounded_unlocked(const T& element, size_t max_index)
    {
        const auto index = detail::load_size<ThreadSafe>(m_size);
        if (index > max_index)
            throw std::length_error("SegmentedVector: index is too large.");
        emplace_back_at_unlocked(index, element);
        return index;
    }

    void pop_back_unlocked()
    {
        ensure_not_empty();
        const auto size = detail::load_size<ThreadSafe>(m_size) - 1;
        detail::store_size<ThreadSafe>(m_size, size);
        std::destroy_at(element(size));
    }

    /// Destroys the elements in [new_size, old_size), last first.
    void destroy_tail(size_t new_size, size_t old_size) noexcept(std::is_nothrow_destructible_v<T>)
    {
        while (old_size > new_size)
            std::destroy_at(element(--old_size));
    }

    template<typename Construct>
    void resize_unlocked(size_t new_size, Construct&& construct)
    {
        const auto old_size = detail::load_size<ThreadSafe>(m_size);
        if (new_size == old_size)
            return;

        if (new_size < old_size)
        {
            detail::store_size<ThreadSafe>(m_size, new_size);
            destroy_tail(new_size, old_size);
            return;
        }

        resize_to_fit(new_size);
        auto size = old_size;
        try
        {
            for (; size < new_size; ++size)
                construct(element(size));
        }
        catch (...)
        {
            destroy_tail(old_size, size);
            throw;
        }
        detail::store_size<ThreadSafe>(m_size, new_size);
    }

    void release() noexcept
    {
        destroy_tail(0, detail::load_size<ThreadSafe>(m_size));
        for (size_t i = 0; i < m_num_segments; ++i)
        {
            ::operator delete(segment(i), std::align_val_t { alignof(T) });
            detail::store_pointer<ThreadSafe, T>(m_segments[i], nullptr);
        }
        m_num_segments = 0;
        detail::store_size<ThreadSafe>(m_capacity, 0);
        detail::store_size<ThreadSafe>(m_size, 0);
    }

    void take(SegmentedVector& other) noexcept
    {
        for (size_t i = 0; i < other.m_num_segments; ++i)
        {
            detail::store_pointer<ThreadSafe, T>(m_segments[i], other.segment(i));
            detail::store_pointer<ThreadSafe, T>(other.m_segments[i], nullptr);
        }
        m_num_segments = std::exchange(other.m_num_segments, 0);
        detail::store_size<ThreadSafe>(m_capacity, other.capacity());
        detail::store_size<ThreadSafe>(m_size, other.size());
        detail::store_size<ThreadSafe>(other.m_capacity, 0);
        detail::store_size<ThreadSafe>(other.m_size, 0);
    }

public:
    template<typename Vector>
    class BasicIterator;

    using iterator = BasicIterator<SegmentedVector>;
    using const_iterator = BasicIterator<const SegmentedVector>;

    static constexpr bool thread_safe = ThreadSafe;

    SegmentedVector() : m_segments {}, m_num_segments(0), m_capacity(0), m_size(0) {}

    SegmentedVector(const SegmentedVector&) = delete;
    SegmentedVector& operator=(const SegmentedVector&) = delete;
    SegmentedVector(SegmentedVector&& other) noexcept : SegmentedVector() { take(other); }

    SegmentedVector& operator=(SegmentedVector&& other) noexcept
    {
        if (this == &other)
            return *this;
        release();
        take(other);
        return *this;
    }

    ~SegmentedVector() { release(); }

    template<typename Vector>
    class BasicIterator
    {
    private:
        using vector_type = Vector;
        using vector_pointer = Vector*;

    public:
        using difference_type = std::ptrdiff_t;
        using value_type = T;
        using reference = std::conditional_t<std::is_const_v<vector_type>, const T&, T&>;
        using pointer = std::conditional_t<std::is_const_v<vector_type>, const T*, T*>;
        using iterator_category = std::random_access_iterator_tag;
        using iterator_concept = std::random_access_iterator_tag;

        BasicIterator() noexcept : m_pos(0), m_vector(nullptr) {}
        BasicIterator(vector_type& vector, size_t pos) noexcept : m_pos(pos), m_vector(&vector) {}

        reference operator*() const { return (*m_vector)[m_pos]; }
        pointer operator->() const { return std::addressof(**this); }

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
        vector_pointer m_vector;
    };

    void clear() noexcept(std::is_nothrow_destructible_v<T>)
    {
        const auto size = detail::load_size<ThreadSafe>(m_size);
        detail::store_size<ThreadSafe>(m_size, 0);
        destroy_tail(0, size);
    }

    template<typename... Args>
    T& emplace_back(Args&&... args)
    {
        return detail::with_lock<ThreadSafe>(m_writer_mutex, [&]() -> T& { return emplace_back_unlocked(std::forward<Args>(args)...); });
    }

    void push_back(const T& element) { emplace_back(element); }

    void push_back(T&& element) { emplace_back(std::move(element)); }

    void resize(size_t size)
    {
        detail::with_lock<ThreadSafe>(m_writer_mutex, [&] { resize_unlocked(size, [](T* element) { std::construct_at(element); }); });
    }

    void resize(size_t size, const T& value)
    {
        detail::with_lock<ThreadSafe>(m_writer_mutex, [&] { resize_unlocked(size, [&](T* element) { std::construct_at(element, value); }); });
    }

    /// Appends only if the returned index is at most max_index.
    size_t push_back_bounded(const T& element, size_t max_index)
    {
        return detail::with_lock<ThreadSafe>(m_writer_mutex, [&] { return push_back_bounded_unlocked(element, max_index); });
    }

    /// Checks the bound and prepares storage, then runs the factory inside the append transaction with the reserved index and publishes.
    /// ThreadSafe holds the writer lock; the factory must not access this vector.
    template<typename Factory>
    size_t emplace_back_with_index(Factory&& factory, size_t max_index)
    {
        return detail::with_lock<ThreadSafe>(m_writer_mutex,
                                             [&]
                                             {
                                                 const auto index = detail::load_size<ThreadSafe>(m_size);
                                                 if (index == std::numeric_limits<size_t>::max() || index > max_index)
                                                     throw std::length_error("SegmentedVector: index is too large.");
                                                 resize_to_fit(index + 1);
                                                 emplace_back_at_unlocked(index, std::forward<Factory>(factory)(index));
                                                 return index;
                                             });
    }

    void pop_back()
    {
        detail::with_lock<ThreadSafe>(m_writer_mutex, [&] { pop_back_unlocked(); });
    }

    const T& operator[](size_t pos) const
    {
        assert(pos < size());
        return *element(pos);
    }

    T& operator[](size_t pos)
    {
        assert(pos < size());
        return *element(pos);
    }

    const T& at(size_t pos) const
    {
        if (pos >= size())
            throw std::out_of_range("SegmentedVector::at");
        return *element(pos);
    }

    T& at(size_t pos)
    {
        if (pos >= size())
            throw std::out_of_range("SegmentedVector::at");
        return *element(pos);
    }

    const T& front() const
    {
        ensure_not_empty();
        return (*this)[0];
    }

    T& front()
    {
        ensure_not_empty();
        return (*this)[0];
    }

    const T& back() const
    {
        ensure_not_empty();
        return (*this)[size() - 1];
    }

    T& back()
    {
        ensure_not_empty();
        return (*this)[size() - 1];
    }

    iterator begin() noexcept { return iterator(*this, 0); }
    iterator end() noexcept { return iterator(*this, size()); }
    const_iterator begin() const noexcept { return const_iterator(*this, 0); }
    const_iterator end() const noexcept { return const_iterator(*this, size()); }
    const_iterator cbegin() const noexcept { return const_iterator(*this, 0); }
    const_iterator cend() const noexcept { return const_iterator(*this, size()); }

    size_t memory_usage() const noexcept { return capacity() * sizeof(T); }

    size_t capacity() const noexcept { return detail::load_size<ThreadSafe>(m_capacity); }
    size_t size() const noexcept { return detail::load_size<ThreadSafe>(m_size); }
    bool empty() const noexcept { return size() == 0; }

private:
    void ensure_not_empty() const
    {
        if (empty())
            throw std::out_of_range("SegmentedVector: container is empty.");
    }

    // Segments grow geometrically, i.e., FirstSegmentSize, 2*FirstSegmentSize,
    // 4*FirstSegmentSize, ...
    std::array<detail::Pointer<ThreadSafe, T>, Layout::max_segments> m_segments;
    size_t m_num_segments;
    detail::Size<ThreadSafe> m_capacity;
    detail::Size<ThreadSafe> m_size;
    [[no_unique_address]] detail::Mutex<ThreadSafe> m_writer_mutex;
};

}  // namespace ygg

#endif
