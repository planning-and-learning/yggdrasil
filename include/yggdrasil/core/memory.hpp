/*
 * Copyright (C) 2023 Dominik Drexler and Simon Stahlberg
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

#ifndef YGG_COMMON_MEMORY_HPP_
#define YGG_COMMON_MEMORY_HPP_

#include <cstdint>
#include <fstream>
#include <istream>
#include <limits>
#include <string>

#if defined(__APPLE__)
#include <mach/mach.h>
#endif

namespace ygg
{

#if !defined(__APPLE__)
namespace detail
{
inline int64_t parse_linux_peak_memory_usage_in_kb(std::istream& in)
{
    auto memory_in_kb = int64_t { -1 };
    auto word = std::string();
    while (in >> word)
    {
        if (word == "VmPeak:")
        {
            if (!(in >> memory_in_kb))
                return -1;
            return memory_in_kb;
        }
        in.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
    return -1;
}
}  // namespace detail
#endif

inline int64_t get_peak_memory_usage_in_bytes()
{
    auto memory_in_kb = int64_t { -1 };

#if defined(__APPLE__)
    // Based on http://stackoverflow.com/questions/63166
    task_basic_info t_info;
    mach_msg_type_number_t t_info_count = TASK_BASIC_INFO_COUNT;

    if (task_info(mach_task_self(), TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&t_info), &t_info_count) == KERN_SUCCESS)
    {
        memory_in_kb = t_info.virtual_size / 1024;
    }
#else
    auto procfile = std::ifstream("/proc/self/status");
    memory_in_kb = detail::parse_linux_peak_memory_usage_in_kb(procfile);
#endif

    if (memory_in_kb < 0)
        return -1;
    return memory_in_kb * 1024;
}

}  // namespace ygg

#endif
