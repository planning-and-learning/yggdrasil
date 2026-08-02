#ifndef YGG_CONTAINERS_RAW_VECTOR_POOL_HPP_
#define YGG_CONTAINERS_RAW_VECTOR_POOL_HPP_

#include "yggdrasil/containers/detail/geometric_byte_storage.hpp"
#include "yggdrasil/containers/segmented_vector.hpp"
#include "yggdrasil/core/concepts.hpp"
#include "yggdrasil/core/config.hpp"

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <vector>

namespace ygg
{

/// ThreadSafe permits concurrent insertion, size queries, and reads after
/// publication was observed through size() or external synchronization.
/// Clear, memory inspection, move, and destruction require quiescence.
template<std::unsigned_integral Size, TriviallyCopyable T, size_t FirstSegmentBytes = 1024, bool ThreadSafe = false>
class RawVectorPool
{
public:
    using value_type = T;
    using ConstView = std::span<const T>;
    static constexpr bool thread_safe = ThreadSafe;

private:
    using Storage = detail::GeometricByteStorage<FirstSegmentBytes, alignof(T), false>;

    static constexpr size_t payload_offset = (sizeof(Size) + alignof(T) - 1) / alignof(T) * alignof(T);

    static constexpr size_t max_payload_size() noexcept { return (std::numeric_limits<size_t>::max() - payload_offset) / sizeof(T); }

    static size_t slot_size_bytes(size_t payload_size)
    {
        if (payload_size > max_payload_size())
            throw std::length_error("RawVectorPool: vector byte size exceeds addressable memory.");

        return payload_offset + payload_size * sizeof(T);
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
                std::byte* slot = m_storage.allocate(needed_bytes);
                const auto stored_size = static_cast<Size>(size);
                std::memcpy(slot, &stored_size, sizeof(stored_size));
                if (size > 0)
                    std::memcpy(slot + payload_offset, data, size * sizeof(T));
                return slot;
            },
            std::numeric_limits<uint_t>::max()));
    }

    ConstView operator[](uint_t index) const noexcept
    {
        assert(index < m_index.size());
        const auto* slot = m_index[index];
        auto size = Size {};
        std::memcpy(&size, slot, sizeof(size));
        if (size == 0)
            return {};
        return { std::launder(reinterpret_cast<const T*>(slot + payload_offset)), static_cast<size_t>(size) };
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

    size_t memory_usage() const noexcept { return m_storage.memory_usage() + m_index.memory_usage(); }

    size_t size() const noexcept { return m_index.size(); }
    bool empty() const noexcept { return m_index.empty(); }

    void clear() noexcept
    {
        m_storage.clear();
        m_index.clear();
    }

private:
    Storage m_storage;
    SegmentedVector<std::byte*, 32, ThreadSafe> m_index;
};

}  // namespace ygg

#endif
