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

#ifndef YGG_CONTAINERS_SHARED_OBJECT_POOL_HPP_
#define YGG_CONTAINERS_SHARED_OBJECT_POOL_HPP_

#include "yggdrasil/containers/detail/object_pool_storage.hpp"

#include <atomic>
#include <cassert>
#include <type_traits>
#include <utility>

namespace ygg
{

/**
 * A shared object pool
 */

template<typename T, bool ThreadSafe = false>
class SharedObjectPool;

template<typename T, bool ThreadSafe = false>
struct SharedObjectPoolEntry
{
    detail::Size<ThreadSafe> refcount;
    T object;

    SharedObjectPoolEntry() : refcount(0), object() {}

    template<typename... Args>
    explicit SharedObjectPoolEntry(Args&&... args) : refcount(0), object(std::forward<Args>(args)...)
    {
    }

    template<typename... Args>
    void initialize(Args&&... args)
    {
        object.initialize(std::forward<Args>(args)...);
    }
};

template<typename T, bool ThreadSafe = false>
class SharedObjectPoolPtr
{
private:
    using Entry = SharedObjectPoolEntry<T, ThreadSafe>;

    SharedObjectPool<T, ThreadSafe>* m_pool;
    Entry* m_entry;

private:
    void deallocate() noexcept
    {
        assert(m_pool && m_entry);

        m_pool->free(m_entry);
        m_pool = nullptr;
        m_entry = nullptr;
    }

    void inc_ref_count() noexcept
    {
        assert(m_entry);

        if constexpr (ThreadSafe)
            m_entry->refcount.fetch_add(1, std::memory_order_relaxed);
        else
            ++m_entry->refcount;
    }

    void dec_ref_count()
    {
        assert(m_entry);

        if constexpr (ThreadSafe)
        {
            const auto old = m_entry->refcount.fetch_sub(1, std::memory_order_acq_rel);
            assert(old > 0);
            if (old == 1)
                deallocate();
        }
        else
        {
            assert(m_entry->refcount > 0);
            if (--m_entry->refcount == 0)
                deallocate();
        }
    }

public:
    SharedObjectPoolPtr() noexcept : SharedObjectPoolPtr(nullptr, nullptr) {}

    SharedObjectPoolPtr(SharedObjectPool<T, ThreadSafe>* pool, Entry* object) noexcept : m_pool(pool), m_entry(object)
    {
        if (m_pool && m_entry)
            inc_ref_count();
    }

    SharedObjectPoolPtr(const SharedObjectPoolPtr& other) noexcept : SharedObjectPoolPtr()
    {
        m_pool = other.m_pool;
        m_entry = other.m_entry;

        if (m_pool && m_entry)
            inc_ref_count();
    }

    SharedObjectPoolPtr& operator=(const SharedObjectPoolPtr& other)
    {
        if (this != &other)
        {
            if (m_pool && m_entry)
                dec_ref_count();

            m_pool = other.m_pool;
            m_entry = other.m_entry;

            if (m_pool && m_entry)
                inc_ref_count();
        }
        return *this;
    }

    SharedObjectPoolPtr(SharedObjectPoolPtr&& other) noexcept : m_pool(other.m_pool), m_entry(other.m_entry)
    {
        other.m_pool = nullptr;
        other.m_entry = nullptr;
    }

    SharedObjectPoolPtr& operator=(SharedObjectPoolPtr&& other) noexcept
    {
        if (this != &other)
        {
            if (m_pool && m_entry)
                dec_ref_count();

            m_pool = other.m_pool;
            m_entry = other.m_entry;

            other.m_pool = nullptr;
            other.m_entry = nullptr;
        }
        return *this;
    }

    ~SharedObjectPoolPtr()
    {
        if (m_pool && m_entry)
            dec_ref_count();
    }

    SharedObjectPoolPtr clone() const
        requires std::is_copy_assignable_v<T>
    {
        if (m_pool && m_entry)
        {
            SharedObjectPoolPtr pointer = m_pool->get_or_allocate();
            *pointer = this->operator*();
            return pointer;
        }
        else
        {
            return SharedObjectPoolPtr();
        }
    }

    T& operator*() const noexcept
    {
        assert(m_entry);
        return m_entry->object;
    }

    T* operator->() const noexcept
    {
        assert(m_entry);
        return &m_entry->object;
    }

    T* get() const noexcept { return m_entry ? &m_entry->object : nullptr; }

    size_t ref_count() const noexcept
    {
        assert(m_entry);
        if constexpr (ThreadSafe)
            return m_entry->refcount.load(std::memory_order_acquire);
        else
            return m_entry->refcount;
    }

    explicit operator bool() const noexcept { return m_entry != nullptr; }
};

template<typename T, bool ThreadSafe>
class SharedObjectPool
{
private:
    using Entry = SharedObjectPoolEntry<T, ThreadSafe>;

    detail::ObjectPoolStorage<Entry, ThreadSafe> m_storage;

    void free(Entry* element) noexcept { m_storage.release(element); }

    friend class SharedObjectPoolPtr<T, ThreadSafe>;

public:
    // Handles retain this address, so the pool must remain stationary.
    SharedObjectPool() noexcept = default;
    SharedObjectPool(const SharedObjectPool& other) = delete;
    SharedObjectPool& operator=(const SharedObjectPool& other) = delete;
    SharedObjectPool(SharedObjectPool&& other) = delete;
    SharedObjectPool& operator=(SharedObjectPool&& other) = delete;

    [[nodiscard]] SharedObjectPoolPtr<T, ThreadSafe> get_or_allocate() { return SharedObjectPoolPtr<T, ThreadSafe>(this, m_storage.acquire()); }

    template<typename... Args>
    [[nodiscard]] SharedObjectPoolPtr<T, ThreadSafe> get_or_allocate(Args&&... args)
    {
        // Only pool bookkeeping is serialized; the checked-out entry cannot
        // be observed by another handle while user initialization runs.
        auto element = get_or_allocate();
        element->initialize(std::forward<Args>(args)...);
        return element;
    }

    [[nodiscard]] size_t size() const noexcept { return m_storage.size(); }

    [[nodiscard]] size_t get_size() const noexcept { return size(); }

    [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    [[nodiscard]] size_t free_size() const noexcept { return m_storage.free_size(); }

    [[nodiscard]] size_t get_num_free() const noexcept { return free_size(); }
};

}  // namespace ygg

#endif
