#ifndef YGG_COMMON_RAW_VECTOR_POOL_HPP_
#define YGG_COMMON_RAW_VECTOR_POOL_HPP_

#include "ygg/core/bit.hpp"
#include "ygg/core/concepts.hpp"
#include "ygg/core/config.hpp"

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <new>
#include <vector>

namespace ygg
{

template<std::unsigned_integral Size, TriviallyCopyable T>
class RawVectorView
{
public:
    RawVectorView() noexcept : m_ptr(nullptr) {}
    explicit RawVectorView(std::byte* ptr) noexcept : m_ptr(ptr) {}

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

    T& front() noexcept
    {
        assert(!empty());
        return data()[0];
    }

    const T& front() const noexcept
    {
        assert(!empty());
        return data()[0];
    }

    T& back() noexcept
    {
        assert(!empty());
        return data()[size() - 1];
    }

    const T& back() const noexcept
    {
        assert(!empty());
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

    std::byte* raw_data() noexcept { return m_ptr; }
    const std::byte* raw_data() const noexcept { return m_ptr; }

private:
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

    const T& front() const noexcept
    {
        assert(!empty());
        return data()[0];
    }

    const T& back() const noexcept
    {
        assert(!empty());
        return data()[size() - 1];
    }

    const T& operator[](size_t i) const noexcept
    {
        assert(i < size());
        return data()[i];
    }

    const std::byte* raw_data() const noexcept { return m_ptr; }

private:
    static constexpr size_t align_up(size_t n, size_t a) noexcept { return (n + a - 1) / a * a; }

    static constexpr size_t payload_offset() noexcept { return align_up(sizeof(Size), alignof(T)); }

    const std::byte* m_ptr;
};

template<std::unsigned_integral Size, TriviallyCopyable T, size_t FirstSegmentBytes = 1024>
class RawVectorPool
{
    static_assert(bit::is_power_of_two(FirstSegmentBytes));

private:
    static constexpr size_t align_up(size_t n, size_t a) noexcept { return (n + a - 1) / a * a; }

    static constexpr size_t payload_offset() noexcept { return align_up(sizeof(Size), alignof(T)); }

    static constexpr size_t slot_size_bytes(size_t payload_size) noexcept { return payload_offset() + payload_size * sizeof(T); }

    static void write_size(std::byte* ptr, Size size) noexcept { std::memcpy(ptr, &size, sizeof(Size)); }

    static T* payload_ptr(std::byte* ptr) noexcept { return std::launder(reinterpret_cast<T*>(ptr + payload_offset())); }

    struct Segment
    {
        std::vector<std::byte> storage;
        size_t used_bytes = 0;

        explicit Segment(size_t num_bytes) : storage(num_bytes), used_bytes(0) {}

        size_t capacity_bytes() const noexcept { return storage.size(); }
        size_t remaining_bytes() const noexcept { return storage.size() - used_bytes; }

        std::byte* allocate(size_t num_bytes) noexcept
        {
            assert(used_bytes + num_bytes <= storage.size());
            std::byte* ptr = storage.data() + used_bytes;
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

        size_t next_bytes = m_segments.empty() ? FirstSegmentBytes : m_segments.back().capacity_bytes() * 2;
        while (next_bytes < needed_bytes)
            next_bytes *= 2;

        m_segments.emplace_back(next_bytes);
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
        if (size > std::numeric_limits<Size>::max())
            throw std::out_of_range("RawVectorPool: vector length exceeds size type.");

        const size_t needed_bytes = slot_size_bytes(size);
        ensure_current_segment(needed_bytes);

        std::byte* slot = m_segments.back().allocate(needed_bytes);
        write_size(slot, static_cast<Size>(size));

        if (size > 0)
            std::memcpy(payload_ptr(slot), data, size * sizeof(T));

        m_index.push_back(slot);
        return to_uint_t(m_index.size() - 1);
    }

    RawVectorView<Size, T> operator[](uint_t index) noexcept
    {
        assert(index < m_index.size());
        return RawVectorView<Size, T>(m_index[index]);
    }

    RawVectorView<const Size, const T> operator[](uint_t index) const noexcept
    {
        assert(index < m_index.size());
        return RawVectorView<const Size, const T>(m_index[index]);
    }

    RawVectorView<Size, T> front() noexcept
    {
        assert(!empty());
        return (*this)[0];
    }

    RawVectorView<const Size, const T> front() const noexcept
    {
        assert(!empty());
        return (*this)[0];
    }

    RawVectorView<Size, T> back() noexcept
    {
        assert(!empty());
        return (*this)[to_uint_t(size() - 1)];
    }

    RawVectorView<const Size, const T> back() const noexcept
    {
        assert(!empty());
        return (*this)[to_uint_t(size() - 1)];
    }

    size_t memory_usage() const noexcept
    {
        size_t bytes = 0;
        for (const auto& seg : m_segments)
            bytes += seg.storage.capacity() * sizeof(std::byte);
        bytes += m_index.capacity() * sizeof(std::byte*);
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
    std::vector<std::byte*> m_index;
};

}  // namespace ygg

#endif