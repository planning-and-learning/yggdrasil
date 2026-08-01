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
#include <cstddef>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace ygg
{
/// ThreadSafe permits concurrent appends, size queries, and reads of published
/// elements. Clear, pop_back, iteration, memory inspection, move, destruction,
/// and mutation of published elements require quiescence or external locking.
template<typename T, size_t FirstSegmentSize = 32, bool ThreadSafe = false>
class SegmentedVector
{
private:
    using Layout = detail::GeometricSegmentLayout<FirstSegmentSize>;

    struct Segment
    {
        T* data = nullptr;
        size_t capacity = 0;
        size_t size = 0;

        explicit Segment(size_t capacity_) :
            data(static_cast<T*>(::operator new(capacity_ * sizeof(T), std::align_val_t { alignof(T) }))),
            capacity(capacity_),
            size(0)
        {
        }

        Segment(const Segment&) = delete;
        Segment& operator=(const Segment&) = delete;

        Segment(Segment&& other) noexcept : data(other.data), capacity(other.capacity), size(other.size)
        {
            other.data = nullptr;
            other.capacity = 0;
            other.size = 0;
        }

        Segment& operator=(Segment&& other) noexcept
        {
            if (this == &other)
                return *this;

            destroy_all();
            deallocate();

            data = other.data;
            capacity = other.capacity;
            size = other.size;

            other.data = nullptr;
            other.capacity = 0;
            other.size = 0;

            return *this;
        }

        ~Segment()
        {
            destroy_all();
            deallocate();
        }

        void destroy_at(size_t pos) noexcept(std::is_nothrow_destructible_v<T>)
        {
            std::destroy_at(data + pos);
            --size;
        }

        void destroy_all() noexcept(std::is_nothrow_destructible_v<T>)
        {
            for (size_t i = 0; i < size; ++i)
                std::destroy_at(data + i);
            size = 0;
        }

        void deallocate() noexcept
        {
            if (data)
                ::operator delete(data, std::align_val_t { alignof(T) });
        }
    };

    class ConcurrentSegments
    {
    public:
        ConcurrentSegments()
        {
            for (auto& segment : m_published)
                segment.store(nullptr, std::memory_order_relaxed);
        }

        ConcurrentSegments(const ConcurrentSegments&) = delete;
        ConcurrentSegments& operator=(const ConcurrentSegments&) = delete;

        ConcurrentSegments(ConcurrentSegments&& other) noexcept : ConcurrentSegments()
        {
            m_owned = std::move(other.m_owned);
            const auto count = m_owned.size();
            for (size_t i = 0; i < count; ++i)
                m_published[i].store(m_owned[i].get(), std::memory_order_relaxed);
            m_size.store(count, std::memory_order_relaxed);
            other.m_size.store(0, std::memory_order_relaxed);
        }

        ConcurrentSegments& operator=(ConcurrentSegments&& other) noexcept
        {
            if (this == &other)
                return *this;
            this->~ConcurrentSegments();
            std::construct_at(this, std::move(other));
            return *this;
        }

        void emplace_back(size_t capacity)
        {
            const auto index = size();
            assert(index < Layout::max_segments);
            m_owned.push_back(std::make_unique<Segment>(capacity));
            m_published[index].store(m_owned.back().get(), std::memory_order_release);
            m_size.store(index + 1, std::memory_order_release);
        }

        Segment& operator[](size_t index) noexcept
        {
            auto* segment = m_published[index].load(std::memory_order_acquire);
            assert(segment);
            return *segment;
        }

        const Segment& operator[](size_t index) const noexcept
        {
            const auto* segment = m_published[index].load(std::memory_order_acquire);
            assert(segment);
            return *segment;
        }

        Segment& at(size_t index)
        {
            if (index >= size())
                throw std::out_of_range("SegmentedVector segment index");
            return (*this)[index];
        }

        const Segment& at(size_t index) const
        {
            if (index >= size())
                throw std::out_of_range("SegmentedVector segment index");
            return (*this)[index];
        }

        bool empty() const noexcept { return size() == 0; }
        size_t size() const noexcept { return m_size.load(std::memory_order_acquire); }

    private:
        std::vector<std::unique_ptr<Segment>> m_owned;
        std::array<std::atomic<Segment*>, Layout::max_segments> m_published;
        std::atomic_size_t m_size { 0 };
    };

    using Segments = std::conditional_t<ThreadSafe, ConcurrentSegments, std::vector<Segment>>;

    void resize_to_fit(size_t n)
    {
        auto capacity = detail::load_size<ThreadSafe>(m_capacity);
        while (capacity < n)
        {
            const auto segment = m_segments.size();
            if (segment >= Layout::max_segments)
                throw std::length_error("SegmentedVector: segment is too large.");

            const auto new_segment_size = Layout::segment_capacity(segment);
            if (new_segment_size > std::numeric_limits<size_t>::max() - capacity || new_segment_size > std::numeric_limits<size_t>::max() / sizeof(T))
                throw std::length_error("SegmentedVector: segment is too large.");

            m_segments.emplace_back(new_segment_size);
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

        const auto index = Layout::segment_index(size);
        const auto offset = Layout::segment_offset(size, index);
        auto* element = std::construct_at(m_segments[index].data + offset, std::forward<Args>(args)...);

        ++m_segments[index].size;
        detail::store_size<ThreadSafe>(m_size, size + 1);
        return *element;
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

        const auto index = Layout::segment_index(size);
        const auto offset = Layout::segment_offset(size, index);
        m_segments[index].destroy_at(offset);
    }

public:
    template<typename Vector>
    class BasicIterator;

    using iterator = BasicIterator<SegmentedVector>;
    using const_iterator = BasicIterator<const SegmentedVector>;

    static constexpr bool thread_safe = ThreadSafe;

    SegmentedVector() : m_segments(), m_capacity(0), m_size(0) {}

    SegmentedVector(const SegmentedVector&) = delete;
    SegmentedVector& operator=(const SegmentedVector&) = delete;
    SegmentedVector(SegmentedVector&& other) noexcept : m_segments(std::move(other.m_segments)), m_capacity(other.capacity()), m_size(other.size())
    {
        detail::store_size<ThreadSafe>(other.m_capacity, 0);
        detail::store_size<ThreadSafe>(other.m_size, 0);
    }

    SegmentedVector& operator=(SegmentedVector&& other) noexcept
    {
        if (this == &other)
            return *this;
        m_segments = std::move(other.m_segments);
        detail::store_size<ThreadSafe>(m_capacity, other.capacity());
        detail::store_size<ThreadSafe>(m_size, other.size());
        detail::store_size<ThreadSafe>(other.m_capacity, 0);
        detail::store_size<ThreadSafe>(other.m_size, 0);
        return *this;
    }

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
        for (size_t i = 0; i < m_segments.size(); ++i)
            m_segments[i].destroy_all();
        detail::store_size<ThreadSafe>(m_size, 0);
    }

    template<typename... Args>
    T& emplace_back(Args&&... args)
    {
        return detail::with_lock<ThreadSafe>(m_writer_mutex, [&]() -> T& { return emplace_back_unlocked(std::forward<Args>(args)...); });
    }

    void push_back(const T& element) { emplace_back(element); }

    void push_back(T&& element) { emplace_back(std::move(element)); }

    /// Appends only if the returned index is at most max_index.
    size_t push_back_bounded(const T& element, size_t max_index)
    {
        return detail::with_lock<ThreadSafe>(m_writer_mutex, [&] { return push_back_bounded_unlocked(element, max_index); });
    }

    void pop_back()
    {
        detail::with_lock<ThreadSafe>(m_writer_mutex, [&] { pop_back_unlocked(); });
    }

    const T& operator[](size_t pos) const
    {
        assert(pos < size());

        const auto index = Layout::segment_index(pos);
        const auto offset = Layout::segment_offset(pos, index);
        return m_segments[index].data[offset];
    }

    T& operator[](size_t pos)
    {
        assert(pos < size());

        const auto index = Layout::segment_index(pos);
        const auto offset = Layout::segment_offset(pos, index);
        return m_segments[index].data[offset];
    }

    const T& at(size_t pos) const
    {
        if (pos >= size())
            throw std::out_of_range("SegmentedVector::at");

        const auto index = Layout::segment_index(pos);
        const auto offset = Layout::segment_offset(pos, index);
        return m_segments.at(index).data[offset];
    }

    T& at(size_t pos)
    {
        if (pos >= size())
            throw std::out_of_range("SegmentedVector::at");

        const auto index = Layout::segment_index(pos);
        const auto offset = Layout::segment_offset(pos, index);
        return m_segments.at(index).data[offset];
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

    size_t memory_usage() const noexcept
    {
        size_t bytes = 0;
        for (size_t i = 0; i < m_segments.size(); ++i)
            bytes += m_segments[i].capacity * sizeof(T);
        return bytes;
    }

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
    Segments m_segments;
    detail::Size<ThreadSafe> m_capacity;
    detail::Size<ThreadSafe> m_size;
    [[no_unique_address]] detail::Mutex<ThreadSafe> m_writer_mutex;
};

}  // namespace ygg

#endif
