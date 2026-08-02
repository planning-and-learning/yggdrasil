/*
 * Copyright (C) 2026 Dominik Drexler
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

#ifndef YGG_CONTAINERS_DETAIL_OBJECT_POOL_STORAGE_HPP_
#define YGG_CONTAINERS_DETAIL_OBJECT_POOL_STORAGE_HPP_

#include "yggdrasil/containers/detail/threading.hpp"

#include <cassert>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace ygg::detail
{

template<typename Entry, bool ThreadSafe>
class ObjectPoolStorage
{
private:
    template<typename Vector>
    static size_t next_capacity(const Vector& vector, size_t required) noexcept
    {
        const auto capacity = vector.capacity();
        const auto doubled = capacity == 0 ? size_t { 1 } : capacity > vector.max_size() / 2 ? vector.max_size() : 2 * capacity;
        return doubled < required ? required : doubled;
    }

    Entry* allocate_unlocked()
    {
        const auto required = m_storage.size() + 1;
        if (m_free.capacity() < required)
            m_free.reserve(next_capacity(m_free, required));
        if (m_storage.capacity() < required)
            m_storage.reserve(next_capacity(m_storage, required));

        auto entry = std::make_unique<Entry>();
        auto* result = entry.get();
        m_storage.push_back(std::move(entry));
        assert(m_free.capacity() >= m_storage.size());
        return result;
    }

public:
    ObjectPoolStorage() noexcept = default;
    ObjectPoolStorage(const ObjectPoolStorage&) = delete;
    ObjectPoolStorage& operator=(const ObjectPoolStorage&) = delete;
    ObjectPoolStorage(ObjectPoolStorage&&) = delete;
    ObjectPoolStorage& operator=(ObjectPoolStorage&&) = delete;

    Entry* acquire()
    {
        return with_lock<ThreadSafe>(m_mutex,
                                     [&]
                                     {
                                         if (m_free.empty())
                                             return allocate_unlocked();
                                         auto* result = m_free.back();
                                         m_free.pop_back();
                                         return result;
                                     });
    }

    void release(Entry* entry) noexcept
    {
        with_lock<ThreadSafe>(m_mutex,
                              [&]
                              {
                                  assert(entry);
                                  assert(m_free.size() < m_storage.size());
                                  assert(m_free.capacity() >= m_storage.size());
                                  m_free.push_back(entry);
                              });
    }

    size_t size() const noexcept
    {
        return with_lock<ThreadSafe>(m_mutex, [&] { return m_storage.size(); });
    }

    size_t free_size() const noexcept
    {
        return with_lock<ThreadSafe>(m_mutex, [&] { return m_free.size(); });
    }

private:
    std::vector<std::unique_ptr<Entry>> m_storage;
    std::vector<Entry*> m_free;
    [[no_unique_address]] mutable Mutex<ThreadSafe> m_mutex;
};

}  // namespace ygg::detail

#endif
