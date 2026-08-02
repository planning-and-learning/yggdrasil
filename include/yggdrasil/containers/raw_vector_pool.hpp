#ifndef YGG_CONTAINERS_RAW_VECTOR_POOL_HPP_
#define YGG_CONTAINERS_RAW_VECTOR_POOL_HPP_

#include "yggdrasil/containers/segmented_vector.hpp"
#include "yggdrasil/core/bit.hpp"
#include "yggdrasil/core/concepts.hpp"
#include "yggdrasil/core/config.hpp"

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <vector>

namespace ygg
{

template<std::unsigned_integral Size, TriviallyCopyable T>
class RawVectorView
{
public:
    RawVectorView() noexcept : m_ptr(nullptr) {}
    explicit RawVectorView(std::byte* ptr) noexcept : m_ptr(ptr) {}

    bool valid() const noexcept { return m_ptr != nullptr; }
    explicit operator bool() const noexcept { return valid(); }

    size_t size() const noexcept
    {
        assert(m_ptr);
        Size value;
        std::memcpy(&value, m_ptr, sizeof(Size));
        return static_cast<size_t>(value);
    }

    bool empty() const noexcept { return size() == 0; }

    T* data() noexcept
    {
        assert(m_ptr);
        return std::launder(reinterpret_cast<T*>(m_ptr + payload_offset()));
    }

    const T* data() const noexcept
    {
        assert(m_ptr);
        return std::launder(reinterpret_cast<const T*>(m_ptr + payload_offset()));
    }

    T* begin() noexcept { return data(); }
    const T* begin() const noexcept { return data(); }
    const T* cbegin() const noexcept { return data(); }
    T* end() noexcept { return data() + size(); }
    const T* end() const noexcept { return data() + size(); }
    const T* cend() const noexcept { return data() + size(); }

    T& front()
    {
        ensure_not_empty();
        return data()[0];
    }

    const T& front() const
    {
        ensure_not_empty();
        return data()[0];
    }

    T& back()
    {
        ensure_not_empty();
        return data()[size() - 1];
    }

    const T& back() const
    {
        ensure_not_empty();
        return data()[size() - 1];
    }

    T& operator[](size_t i) noexcept
    {
        assert(i < size());
        return data()[i];
    }

    const T& operator[](size_t i) const noexcept
    {
        assert(i < size());
        return data()[i];
    }

    T& at(size_t i)
    {
        ensure_index(i);
        return (*this)[i];
    }

    const T& at(size_t i) const
    {
        ensure_index(i);
        return (*this)[i];
    }

    std::byte* raw_data() noexcept { return m_ptr; }
    const std::byte* raw_data() const noexcept { return m_ptr; }

private:
    void ensure_index(size_t i) const
    {
        if (!valid())
            throw std::logic_error("RawVectorView: invalid view.");
        if (i >= size())
            throw std::out_of_range("RawVectorView: index out of range.");
    }

    void ensure_not_empty() const
    {
        if (!valid())
            throw std::logic_error("RawVectorView: invalid view.");
        if (empty())
            throw std::out_of_range("RawVectorView: view is empty.");
    }

    static constexpr size_t align_up(size_t n, size_t a) noexcept { return (n + a - 1) / a * a; }

    static constexpr size_t payload_offset() noexcept { return align_up(sizeof(Size), alignof(T)); }

    std::byte* m_ptr;
};

template<std::unsigned_integral Size, TriviallyCopyable T>
class RawVectorView<const Size, const T>
{
public:
    RawVectorView() noexcept : m_ptr(nullptr) {}
    explicit RawVectorView(const std::byte* ptr) noexcept : m_ptr(ptr) {}
    RawVectorView(const RawVectorView<Size, T>& other) noexcept : m_ptr(other.raw_data()) {}

    bool valid() const noexcept { return m_ptr != nullptr; }
    explicit operator bool() const noexcept { return valid(); }

    size_t size() const noexcept
    {
        assert(m_ptr);
        Size value;
        std::memcpy(&value, m_ptr, sizeof(Size));
        return static_cast<size_t>(value);
    }

    bool empty() const noexcept { return size() == 0; }

    const T* data() const noexcept
    {
        assert(m_ptr);
        return std::launder(reinterpret_cast<const T*>(m_ptr + payload_offset()));
    }

    const T* begin() const noexcept { return data(); }
    const T* cbegin() const noexcept { return data(); }
    const T* end() const noexcept { return data() + size(); }
    const T* cend() const noexcept { return data() + size(); }

    const T& front() const
    {
        ensure_not_empty();
        return data()[0];
    }

    const T& back() const
    {
        ensure_not_empty();
        return data()[size() - 1];
    }

    const T& operator[](size_t i) const noexcept
    {
        assert(i < size());
        return data()[i];
    }

    const T& at(size_t i) const
    {
        ensure_index(i);
        return (*this)[i];
    }

    const std::byte* raw_data() const noexcept { return m_ptr; }

private:
    void ensure_index(size_t i) const
    {
        if (!valid())
            throw std::logic_error("RawVectorView: invalid view.");
        if (i >= size())
            throw std::out_of_range("RawVectorView: index out of range.");
    }

    void ensure_not_empty() const
    {
        if (!valid())
            throw std::logic_error("RawVectorView: invalid view.");
        if (empty())
            throw std::out_of_range("RawVectorView: view is empty.");
    }

    static constexpr size_t align_up(size_t n, size_t a) noexcept { return (n + a - 1) / a * a; }

    static constexpr size_t payload_offset() noexcept { return align_up(sizeof(Size), alignof(T)); }

    const std::byte* m_ptr;
};

/// ThreadSafe permits concurrent insertion, size queries, and reads after
/// publication was observed through size() or external synchronization.
/// Clear, memory inspection, move, and destruction require quiescence.
template<std::unsigned_integral Size, TriviallyCopyable T, size_t FirstSegmentBytes = 1024, bool ThreadSafe = false>
class RawVectorPool
{
    static_assert(bit::is_power_of_two(FirstSegmentBytes));

public:
    using value_type = T;
    using ConstView = RawVectorView<const Size, const T>;
    static constexpr bool thread_safe = ThreadSafe;

private:
    static constexpr size_t align_up(size_t n, size_t a) noexcept { return (n + a - 1) / a * a; }

    static constexpr size_t payload_offset() noexcept { return align_up(sizeof(Size), alignof(T)); }

    static constexpr size_t max_payload_size() noexcept { return (std::numeric_limits<size_t>::max() - payload_offset()) / sizeof(T); }

    static size_t slot_size_bytes(size_t payload_size)
    {
        if (payload_size > max_payload_size())
            throw std::length_error("RawVectorPool: vector byte size exceeds addressable memory.");

        return payload_offset() + payload_size * sizeof(T);
    }

    static void write_size(std::byte* ptr, Size size) noexcept { std::memcpy(ptr, &size, sizeof(Size)); }

    static T* payload_ptr(std::byte* ptr) noexcept { return std::launder(reinterpret_cast<T*>(ptr + payload_offset())); }

    struct Segment
    {
        std::unique_ptr<std::byte[]> storage;
        size_t capacity;
        size_t used_bytes = 0;

        explicit Segment(size_t num_bytes) : storage(std::make_unique_for_overwrite<std::byte[]>(num_bytes)), capacity(num_bytes), used_bytes(0) {}

        size_t capacity_bytes() const noexcept { return capacity; }
        size_t remaining_bytes() const noexcept { return capacity - used_bytes; }

        std::byte* allocate(size_t num_bytes) noexcept
        {
            assert(num_bytes <= remaining_bytes());
            std::byte* ptr = storage.get() + used_bytes;
            used_bytes += num_bytes;
            return ptr;
        }

        void clear() noexcept { used_bytes = 0; }
    };

    bool current_segment_fits(size_t needed_bytes) const noexcept { return !m_segments.empty() && m_segments.back().remaining_bytes() >= needed_bytes; }

    void ensure_current_segment(size_t needed_bytes)
    {
        if (current_segment_fits(needed_bytes))
            return;

        size_t next_bytes = FirstSegmentBytes;
        if (!m_segments.empty())
        {
            const auto current_bytes = m_segments.back().capacity_bytes();
            next_bytes = current_bytes > std::numeric_limits<size_t>::max() / 2 ? needed_bytes : current_bytes * 2;
        }

        while (next_bytes < needed_bytes)
        {
            if (next_bytes > std::numeric_limits<size_t>::max() / 2)
            {
                next_bytes = needed_bytes;
                break;
            }
            next_bytes *= 2;
        }

        m_segments.emplace_back(next_bytes);
    }

private:
    void ensure_index(uint_t index) const
    {
        if (index >= m_index.size())
            throw std::out_of_range("RawVectorPool: index out of range.");
    }

    void ensure_not_empty() const
    {
        if (empty())
            throw std::out_of_range("RawVectorPool: container is empty.");
    }

public:
    RawVectorPool() = default;

    RawVectorPool(const RawVectorPool&) = delete;
    RawVectorPool& operator=(const RawVectorPool&) = delete;
    RawVectorPool(RawVectorPool&&) = default;
    RawVectorPool& operator=(RawVectorPool&&) = default;

    uint_t insert(std::span<const T> value) { return insert(value.data(), value.size()); }

    uint_t insert(const std::vector<T>& value) { return insert(std::span<const T>(value)); }

    uint_t insert(const T* data, size_t size)
    {
        if (data == nullptr && size > 0)
            throw std::invalid_argument("RawVectorPool: non-empty vector insert requires non-null data.");
        if (size > std::numeric_limits<Size>::max())
            throw std::out_of_range("RawVectorPool: vector length exceeds size type.");

        const size_t needed_bytes = slot_size_bytes(size);
        return static_cast<uint_t>(m_index.emplace_back_with_index(
            [&](size_t)
            {
                ensure_current_segment(needed_bytes);

                std::byte* slot = m_segments.back().allocate(needed_bytes);
                write_size(slot, static_cast<Size>(size));
                if (size > 0)
                    std::memcpy(payload_ptr(slot), data, size * sizeof(T));
                return slot;
            },
            std::numeric_limits<uint_t>::max()));
    }

    ConstView operator[](uint_t index) const noexcept
    {
        assert(index < m_index.size());
        return ConstView(m_index[index]);
    }

    ConstView at(uint_t index) const
    {
        ensure_index(index);
        return (*this)[index];
    }

    ConstView front() const
    {
        ensure_not_empty();
        return (*this)[0];
    }

    ConstView back() const
    {
        ensure_not_empty();
        return (*this)[to_uint_t(size() - 1)];
    }

    size_t memory_usage() const noexcept
    {
        size_t bytes = 0;
        for (const auto& seg : m_segments)
            bytes += seg.capacity_bytes();
        bytes += m_index.memory_usage();
        return bytes;
    }

    size_t size() const noexcept { return m_index.size(); }
    bool empty() const noexcept { return m_index.empty(); }

    void clear() noexcept
    {
        for (auto& seg : m_segments)
            seg.clear();
        m_index.clear();
    }

private:
    std::vector<Segment> m_segments;
    SegmentedVector<std::byte*, 32, ThreadSafe> m_index;
};

}  // namespace ygg

#endif
