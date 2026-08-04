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

#ifndef YGG_CORE_PORTABLE_SHUFFLE_HPP_
#define YGG_CORE_PORTABLE_SHUFFLE_HPP_

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <random>
#include <stdexcept>
#include <type_traits>

namespace ygg
{
namespace detail
{
template<std::uniform_random_bit_generator Generator>
std::uint64_t portable_uniform_index(Generator& generator, std::uint64_t bound)
{
    if (bound == 0)
        throw std::invalid_argument("portable_uniform_index(...): bound must be positive.");

    using Result = std::make_unsigned_t<typename Generator::result_type>;
    constexpr auto min = static_cast<Result>(Generator::min());
    constexpr auto max = static_cast<Result>(Generator::max());

    if constexpr (min == 0 && max == std::numeric_limits<Result>::max())
    {
        if (bound > static_cast<std::uint64_t>(std::numeric_limits<Result>::max()))
            throw std::invalid_argument("portable_uniform_index(...): bound exceeds generator range.");

        const auto result_bound = static_cast<Result>(bound);
        const auto threshold = static_cast<Result>(-result_bound) % result_bound;

        while (true)
        {
            const auto value = static_cast<Result>(generator());
            if (value >= threshold)
                return value % result_bound;
        }
    }
    else
    {
        const auto range = static_cast<std::uint64_t>(max - min) + 1;
        if (bound > range)
            throw std::invalid_argument("portable_uniform_index(...): bound exceeds generator range.");

        const auto threshold = range % bound;

        while (true)
        {
            const auto value = static_cast<std::uint64_t>(static_cast<Result>(generator()) - min);
            if (value >= threshold)
                return value % bound;
        }
    }
}
}

template<std::random_access_iterator Iterator, std::uniform_random_bit_generator Generator>
void portable_shuffle(Iterator first, Iterator last, Generator& generator)
{
    auto size = static_cast<std::uint64_t>(last - first);
    while (size > 1)
    {
        --size;
        std::iter_swap(first + size, first + detail::portable_uniform_index(generator, size + 1));
    }
}

}

#endif
