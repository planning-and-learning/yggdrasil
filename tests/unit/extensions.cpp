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

#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <stdexcept>
#include <type_traits>
#include "yggdrasil/core/config.hpp"
#include "yggdrasil/execution/onetbb.hpp"

#include <urd/elements/index.hpp>

namespace ygg::tests
{

struct AdaptersIndexTag;
}

namespace ygg::tests
{

TEST(YggdrasilTests, CommonAdaptersUmbrellaHeaderExposesPublicSurfaces)
{
    static_assert(std::is_same_v<urd::index<AdaptersIndexTag>::value_type, ygg::uint_t>);

    const auto default_index = urd::index<AdaptersIndexTag>();
    const auto max_index = urd::index<AdaptersIndexTag>();
    const auto index = urd::index<AdaptersIndexTag>(3);

    EXPECT_TRUE(default_index.is_none());
    EXPECT_TRUE(max_index.is_none());
    EXPECT_FALSE(index.is_none());
    EXPECT_EQ(index.value, 3);
}

TEST(YggdrasilTests, CommonExecutionContextExposesThreadLimit)
{
    const auto max_num_threads = ygg::ExecutionContext::get_max_num_threads();

    EXPECT_GE(max_num_threads, 1);
    EXPECT_EQ(ygg::ExecutionContext(max_num_threads).get_num_threads(), max_num_threads);
}

TEST(YggdrasilTests, CommonExecutionContextRejectsInvalidThreadCounts)
{
    const auto max_num_threads = ygg::ExecutionContext::get_max_num_threads();

    EXPECT_THROW(ygg::ExecutionContext(0), std::invalid_argument);
    EXPECT_THROW(ygg::ExecutionContext(max_num_threads + 1), std::invalid_argument);
}

}  // namespace ygg::tests
