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

#ifndef YGG_CONTAINERS_DETAIL_THREADING_HPP_
#define YGG_CONTAINERS_DETAIL_THREADING_HPP_

#include <atomic>
#include <cstddef>
#include <mutex>
#include <type_traits>
#include <utility>

namespace ygg::detail
{

template<bool ThreadSafe>
class Mutex;

template<>
class Mutex<false>
{
};

template<>
class Mutex<true>
{
public:
    Mutex() = default;
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    // Moving requires quiescence; the destination keeps an independent lock.
    Mutex(Mutex&&) noexcept {}
    Mutex& operator=(Mutex&&) noexcept { return *this; }

    void lock() { m_mutex.lock(); }
    void unlock() { m_mutex.unlock(); }

private:
    std::mutex m_mutex;
};

template<bool ThreadSafe, typename Fn>
decltype(auto) with_lock([[maybe_unused]] Mutex<ThreadSafe>& mutex, Fn&& fn)
{
    if constexpr (ThreadSafe)
    {
        std::lock_guard lock(mutex);
        return std::forward<Fn>(fn)();
    }
    else
    {
        return std::forward<Fn>(fn)();
    }
}

template<bool ThreadSafe>
using Size = std::conditional_t<ThreadSafe, std::atomic_size_t, size_t>;

template<bool ThreadSafe>
size_t load_size(const Size<ThreadSafe>& value) noexcept
{
    if constexpr (ThreadSafe)
        return value.load(std::memory_order_acquire);
    else
        return value;
}

template<bool ThreadSafe>
void store_size(Size<ThreadSafe>& target, size_t value) noexcept
{
    if constexpr (ThreadSafe)
        target.store(value, std::memory_order_release);
    else
        target = value;
}

}  // namespace ygg::detail

#endif
