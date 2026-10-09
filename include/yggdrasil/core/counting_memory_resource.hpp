/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_CORE_COUNTING_MEMORY_RESOURCE_HPP_
#define YGG_CORE_COUNTING_MEMORY_RESOURCE_HPP_

#include <cstddef>
#include <memory_resource>

namespace ygg
{
/// Counts outstanding requested bytes; upstream must outlive this unsynchronized resource.
/// Used upstream of a pool, this measures retained chunks, not live objects or allocator overhead.
class CountingMemoryResource final : public std::pmr::memory_resource
{
    std::pmr::memory_resource& m_upstream;
    size_t m_bytes = 0;

    void* do_allocate(size_t bytes, size_t alignment) override
    {
        auto* result = m_upstream.allocate(bytes, alignment);
        m_bytes += bytes;
        return result;
    }
    void do_deallocate(void* pointer, size_t bytes, size_t alignment) override
    {
        m_upstream.deallocate(pointer, bytes, alignment);
        m_bytes -= bytes;
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }

public:
    explicit CountingMemoryResource(std::pmr::memory_resource& upstream = *std::pmr::new_delete_resource()) noexcept : m_upstream(upstream) {}

    // Allocators retain this resource's address and allocations belong to its counter.
    CountingMemoryResource(const CountingMemoryResource&) = delete;
    CountingMemoryResource& operator=(const CountingMemoryResource&) = delete;
    CountingMemoryResource(CountingMemoryResource&&) = delete;
    CountingMemoryResource& operator=(CountingMemoryResource&&) = delete;

    size_t memory_usage() const noexcept { return m_bytes; }
};
}  // namespace ygg

#endif
