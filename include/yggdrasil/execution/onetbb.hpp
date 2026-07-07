

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

#ifndef YGG_EXECUTION_ONETBB_HPP_
#define YGG_EXECUTION_ONETBB_HPP_

#include <cstddef>
#include <memory>
#include <oneapi/tbb/info.h>
#include <oneapi/tbb/task_arena.h>
#include <stdexcept>
#include <string>

namespace ygg
{

struct ExecutionContext
{
    using size_type = std::size_t;
    using uint_t = size_type;

    explicit ExecutionContext(uint_t num_threads) : m_num_threads(num_threads), m_arena()
    {
        if (num_threads == 0)
        {
            throw std::invalid_argument("num_threads must be at least 1.");
        }

        const auto available_threads = get_max_num_threads();

        if (num_threads > available_threads)
        {
            throw std::invalid_argument("Requested " + std::to_string(num_threads) + " threads, but only " + std::to_string(available_threads)
                                        + " are available by default.");
        }

        m_arena.initialize(static_cast<int>(num_threads));
    }

    static std::shared_ptr<ExecutionContext> create(uint_t num_threads) { return std::make_shared<ExecutionContext>(num_threads); }

    static uint_t get_max_num_threads() noexcept { return static_cast<uint_t>(oneapi::tbb::info::default_concurrency()); }

    uint_t get_num_threads() const noexcept { return m_num_threads; }

    oneapi::tbb::task_arena& arena() noexcept { return m_arena; }

    const oneapi::tbb::task_arena& arena() const noexcept { return m_arena; }

private:
    uint_t m_num_threads;
    oneapi::tbb::task_arena m_arena;
};

using ExecutionContextPtr = std::shared_ptr<ExecutionContext>;

}  // namespace ygg

#endif
