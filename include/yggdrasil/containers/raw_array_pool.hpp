/*
 * Copyright (C) 2025 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_CONTAINERS_RAW_ARRAY_POOL_HPP_
#define YGG_CONTAINERS_RAW_ARRAY_POOL_HPP_

#include "yggdrasil/containers/detail/geometric_byte_storage.hpp"
#include "yggdrasil/containers/detail/threading.hpp"
#include "yggdrasil/core/concepts.hpp"
#include "yggdrasil/core/config.hpp"

#include <cassert>
#include <cstddef>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <utility>

namespace ygg
{

/// ThreadSafe permits concurrent insertion, size queries, and reads after
/// publication was observed through size() or external synchronization.
/// Clear, memory inspection, move, and destruction require quiescence.
template<TriviallyCopyable T, size_t FirstSegmentSize = 1024, bool ThreadSafe = false>
class RawArrayPool
{
public:
    using value_type = T;
    using ConstView = std::span<const T>;
    static constexpr bool thread_safe = ThreadSafe;

private:
    using Storage = detail::GeometricByteStorage<FirstSegmentSize, alignof(T), ThreadSafe>;

    static size_t array_size_bytes(size_t array_size)
    {
        if (array_size > std::numeric_limits<size_t>::max() / sizeof(T))
            throw std::length_error("RawArrayPool: array size exceeds addressable memory.");
        return array_size * sizeof(T);
    }

private:
    void ensure_index(size_t array_index) const
    {
        if (array_index >= size())
            throw std::out_of_range("RawArrayPool: index out of range.");
    }

    void ensure_not_empty() const
    {
        if (empty())
            throw std::out_of_range("RawArrayPool: container is empty.");
    }

public:
    explicit RawArrayPool(size_t array_size) :
        m_array_size(array_size),
        m_array_size_bytes(array_size_bytes(array_size)),
        m_storage(m_array_size_bytes),
        m_size(0)
    {
    }

    RawArrayPool(const RawArrayPool&) = delete;
    RawArrayPool& operator=(const RawArrayPool&) = delete;

    RawArrayPool(RawArrayPool&& other) noexcept :
        m_array_size(other.m_array_size),
        m_array_size_bytes(other.m_array_size_bytes),
        m_storage(std::move(other.m_storage)),
        m_size(other.size())
    {
        detail::store_size<ThreadSafe>(other.m_size, 0);
    }

    RawArrayPool& operator=(RawArrayPool&& other) noexcept
    {
        if (this == &other)
            return *this;

        const auto other_size = other.size();
        m_array_size = other.m_array_size;
        m_array_size_bytes = other.m_array_size_bytes;
        m_storage = std::move(other.m_storage);
        detail::store_size<ThreadSafe>(m_size, other_size);
        detail::store_size<ThreadSafe>(other.m_size, 0);
        return *this;
    }

    uint_t insert(std::span<const T> value)
    {
        if (value.size() != m_array_size)
            throw std::invalid_argument("RawArrayPool: wrong number of elements.");

        return detail::with_lock<ThreadSafe>(m_writer_mutex,
                                             [&]
                                             {
                                                 const auto index = detail::load_size<ThreadSafe>(m_size);
                                                 if (index == std::numeric_limits<size_t>::max() || index > std::numeric_limits<uint_t>::max())
                                                     throw std::length_error("RawArrayPool: index is too large.");

                                                 if (m_array_size_bytes > 0)
                                                 {
                                                     auto* result = m_storage.allocate(m_array_size_bytes);
                                                     assert(result == m_storage.data_at(index));
                                                     std::memcpy(result, value.data(), m_array_size_bytes);
                                                 }

                                                 detail::store_size<ThreadSafe>(m_size, index + 1);
                                                 return static_cast<uint_t>(index);
                                             });
    }

    ConstView operator[](size_t array_index) const noexcept
    {
        assert(array_index < size());
        if (m_array_size == 0)
            return {};

        return ConstView(std::launder(reinterpret_cast<const T*>(m_storage.data_at(array_index))), m_array_size);
    }

    ConstView at(size_t array_index) const
    {
        ensure_index(array_index);
        return (*this)[array_index];
    }

    ConstView front() const
    {
        ensure_not_empty();
        return (*this)[0];
    }

    ConstView back() const
    {
        ensure_not_empty();
        return (*this)[size() - 1];
    }

    void clear() noexcept
    {
        m_storage.clear();
        detail::store_size<ThreadSafe>(m_size, 0);
    }

    size_t memory_usage() const noexcept { return m_storage.memory_usage(); }

    size_t size() const noexcept { return detail::load_size<ThreadSafe>(m_size); }
    bool empty() const noexcept { return size() == 0; }
    size_t array_size() const noexcept { return m_array_size; }

private:
    size_t m_array_size;
    size_t m_array_size_bytes;
    Storage m_storage;
    detail::Size<ThreadSafe> m_size;
    [[no_unique_address]] detail::Mutex<ThreadSafe> m_writer_mutex;
};

}  // namespace ygg

#endif
