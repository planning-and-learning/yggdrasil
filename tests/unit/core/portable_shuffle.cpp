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

#include "yggdrasil/core/portable_shuffle.hpp"

#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <span>

namespace ygg::tests
{
namespace
{
class FixedGenerator
{
public:
    using result_type = std::uint32_t;

    static constexpr result_type min() { return 0; }
    static constexpr result_type max() { return 9; }

    result_type operator()() { return values[index++]; }

private:
    std::array<result_type, 5> values { 7, 1, 8, 4, 9 };
    std::size_t index = 0;
};
}

TEST(CorePortableShuffleTest, UsesStableFisherYatesOrder)
{
    auto values = std::array<int, 5> { 0, 1, 2, 3, 4 };
    auto generator = FixedGenerator {};

    const auto range = std::span(values);
    portable_shuffle(range.begin(), range.end(), generator);

    EXPECT_EQ(values, (std::array<int, 5> { 3, 4, 1, 0, 2 }));
}

}
