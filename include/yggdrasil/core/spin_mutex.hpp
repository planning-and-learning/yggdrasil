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

#ifndef YGG_CORE_SPIN_MUTEX_HPP_
#define YGG_CORE_SPIN_MUTEX_HPP_

#include <atomic>
#include <cstdint>
#include <sched.h>

namespace ygg
{

namespace detail
{
/// Pauses 1, 2, 4, ..., 16 times, then yields to the scheduler.
class SpinBackoff
{
    static constexpr std::int32_t LOOPS_BEFORE_YIELD = 16;
    std::int32_t m_count = 1;

public:
    void reset() noexcept { m_count = 1; }
    void pause() noexcept
    {
        if (m_count <= LOOPS_BEFORE_YIELD)
        {
            for (auto i = m_count; i > 0; --i)
            {
#if defined(__x86_64__) || defined(__i386__)
                __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__ARM_ARCH_7A__)
                __asm__ __volatile__("isb sy" ::: "memory");
#else
                sched_yield();
#endif
            }
            m_count *= 2;
        }
        else
        {
            sched_yield();
        }
    }
};
}  // namespace detail

// oneTBB's spin_mutex and spin_rw_mutex with their state words. oneTBB's headers pull in <immintrin.h> and <thread>
// for the backoff (about 100 MB in every includer), so the backoff uses compiler builtins.

/// The algorithm and state word of oneTBB's spin_mutex: unfair test-and-test-and-set.
class SpinMutex
{
    std::atomic<bool> m_flag { false };

public:
    SpinMutex() noexcept = default;
    SpinMutex(const SpinMutex&) = delete;
    ~SpinMutex() = default;
    SpinMutex& operator=(const SpinMutex&) = delete;

    void lock() noexcept
    {
        for (detail::SpinBackoff backoff; m_flag.load(std::memory_order_relaxed) || m_flag.exchange(true);)
            backoff.pause();
    }

    bool try_lock() noexcept { return !m_flag.load(std::memory_order_relaxed) && !m_flag.exchange(true); }

    void unlock() noexcept { m_flag.store(false, std::memory_order_release); }
};

/// The algorithm and state word of oneTBB's spin_rw_mutex: unfair and writer-preferred.
class SpinSharedMutex
{
    using State = std::intptr_t;

    static constexpr State WRITER = 1;
    static constexpr State WRITER_PENDING = 2;
    static constexpr State READERS = ~(WRITER | WRITER_PENDING);
    static constexpr State ONE_READER = 4;
    static constexpr State BUSY = WRITER | READERS;

    std::atomic<State> m_state { 0 };

public:
    SpinSharedMutex() noexcept = default;
    SpinSharedMutex(const SpinSharedMutex&) = delete;
    ~SpinSharedMutex() = default;
    SpinSharedMutex& operator=(const SpinSharedMutex&) = delete;

    void lock() noexcept
    {
        for (detail::SpinBackoff backoff;; backoff.pause())
        {
            auto state = m_state.load(std::memory_order_relaxed);
            if (!(state & BUSY))
            {
                if (m_state.compare_exchange_strong(state, WRITER))
                    return;
                backoff.reset();
            }
            else if (!(state & WRITER_PENDING))
            {
                m_state |= WRITER_PENDING;
            }
        }
    }

    bool try_lock() noexcept
    {
        auto state = m_state.load(std::memory_order_relaxed);
        return !(state & BUSY) && m_state.compare_exchange_strong(state, WRITER);
    }

    void unlock() noexcept { m_state &= READERS; }

    void lock_shared() noexcept
    {
        for (detail::SpinBackoff backoff;; backoff.pause())
            if (try_lock_shared())
                return;
    }

    bool try_lock_shared() noexcept
    {
        if (m_state.load(std::memory_order_relaxed) & (WRITER | WRITER_PENDING))
            return false;
        if (!(m_state.fetch_add(ONE_READER) & WRITER))
            return true;
        m_state -= ONE_READER;
        return false;
    }

    void unlock_shared() noexcept { m_state -= ONE_READER; }
};

}  // namespace ygg

#endif
