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

#ifndef YGG_CONTAINERS_UNIQUE_OBJECT_POOL_HPP_
#define YGG_CONTAINERS_UNIQUE_OBJECT_POOL_HPP_

#include "yggdrasil/containers/detail/threading.hpp"

#include <cassert>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace ygg
{
template<typename T, bool ThreadSafe = false>
class UniqueObjectPool;

template<typename T, bool ThreadSafe = false>
class UniqueObjectPoolPtr
{
private:
    UniqueObjectPool<T, ThreadSafe>* m_pool;
    T* m_entry;

private:
    void deallocate() noexcept
    {
        assert(m_pool && m_entry);

        m_pool->free(m_entry);
        m_pool = nullptr;
        m_entry = nullptr;
    }

public:
    UniqueObjectPoolPtr() noexcept : UniqueObjectPoolPtr<T, ThreadSafe>(nullptr, nullptr) {}

    UniqueObjectPoolPtr(UniqueObjectPool<T, ThreadSafe>* pool, T* object) noexcept : m_pool(pool), m_entry(object) {}

    UniqueObjectPoolPtr(const UniqueObjectPoolPtr& other) = delete;

    UniqueObjectPoolPtr& operator=(const UniqueObjectPoolPtr& other) = delete;

    // Movable
    UniqueObjectPoolPtr(UniqueObjectPoolPtr&& other) noexcept : m_pool(other.m_pool), m_entry(other.m_entry)
    {
        other.m_pool = nullptr;
        other.m_entry = nullptr;
    }

    UniqueObjectPoolPtr& operator=(UniqueObjectPoolPtr&& other) noexcept
    {
        if (this != &other)
        {
            if (m_pool && m_entry)
                deallocate();

            m_pool = other.m_pool;
            m_entry = other.m_entry;

            other.m_pool = nullptr;
            other.m_entry = nullptr;
        }
        return *this;
    }

    UniqueObjectPoolPtr clone() const
        requires std::is_copy_assignable_v<T>
    {
        if (m_pool && m_entry)
        {
            UniqueObjectPoolPtr pointer = m_pool->get_or_allocate();
            *pointer = this->operator*();  // copy-assign T
            return pointer;
        }
        else
        {
            return UniqueObjectPoolPtr();
        }
    }

    ~UniqueObjectPoolPtr()
    {
        if (m_pool && m_entry)
            deallocate();
    }

    T& operator*() const noexcept
    {
        assert(m_entry);
        return *m_entry;
    }

    T* operator->() const noexcept
    {
        assert(m_entry);
        return m_entry;
    }

    T* get() const noexcept { return m_entry; }

    explicit operator bool() const noexcept { return m_entry != nullptr; }
};

template<typename T, bool ThreadSafe>
class UniqueObjectPool
{
private:
    std::vector<std::unique_ptr<T>> m_storage;
    std::vector<T*> m_stack;
    [[no_unique_address]] mutable detail::Mutex<ThreadSafe> m_mutex;

    void allocate_unlocked()
    {
        if (m_storage.size() == m_storage.capacity())
        {
            const auto capacity = m_storage.capacity();
            const auto next_capacity = capacity == 0 ? size_t { 1 } : capacity > m_storage.max_size() / 2 ? m_storage.max_size() : 2 * capacity;
            m_storage.reserve(next_capacity);
            m_stack.reserve(next_capacity);
        }

        m_storage.push_back(std::make_unique<T>());
        m_stack.push_back(m_storage.back().get());
    }

    void free(T* element) noexcept
    {
        detail::with_lock<ThreadSafe>(m_mutex,
                                      [&]
                                      {
                                          assert(m_stack.size() < m_stack.capacity());
                                          m_stack.push_back(element);
                                      });
    }

    friend class UniqueObjectPoolPtr<T, ThreadSafe>;

public:
    // Non-copyable to prevent dangling memory pool pointers.
    UniqueObjectPool() noexcept = default;
    UniqueObjectPool(const UniqueObjectPool& other) = delete;
    UniqueObjectPool& operator=(const UniqueObjectPool& other) = delete;
    UniqueObjectPool(UniqueObjectPool&& other) noexcept
        requires(!ThreadSafe)
    = default;
    UniqueObjectPool(UniqueObjectPool&& other) noexcept
        requires ThreadSafe
    = delete;
    UniqueObjectPool& operator=(UniqueObjectPool&& other) noexcept
        requires(!ThreadSafe)
    = default;
    UniqueObjectPool& operator=(UniqueObjectPool&& other) noexcept
        requires ThreadSafe
    = delete;

    [[nodiscard]] UniqueObjectPoolPtr<T, ThreadSafe> get_or_allocate()
    {
        auto* element = detail::with_lock<ThreadSafe>(m_mutex,
                                                      [&]
                                                      {
                                                          if (m_stack.empty())
                                                              allocate_unlocked();
                                                          auto* result = m_stack.back();
                                                          m_stack.pop_back();
                                                          return result;
                                                      });
        return UniqueObjectPoolPtr<T, ThreadSafe>(this, element);
    }

    template<typename... Args>
    [[nodiscard]] UniqueObjectPoolPtr<T, ThreadSafe> get_or_allocate(Args&&... args)
    {
        // Only pool bookkeeping is serialized; the checked-out object is
        // exclusively owned while user initialization runs.
        auto element = get_or_allocate();
        element->initialize(std::forward<Args>(args)...);
        return element;
    }

    [[nodiscard]] size_t size() const noexcept
    {
        return detail::with_lock<ThreadSafe>(m_mutex, [&] { return m_storage.size(); });
    }

    [[nodiscard]] size_t get_size() const noexcept { return size(); }

    [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    [[nodiscard]] size_t free_size() const noexcept
    {
        return detail::with_lock<ThreadSafe>(m_mutex, [&] { return m_stack.size(); });
    }

    [[nodiscard]] size_t get_num_free() const noexcept { return free_size(); }
};

}  // namespace ygg

#endif
