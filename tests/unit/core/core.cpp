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

#include "yggdrasil/core/chrono.hpp"
#include "yggdrasil/core/closed_interval.hpp"
#include "yggdrasil/core/memory.hpp"
#include "yggdrasil/io/project_path.hpp"

#include "yggdrasil/core/config.hpp"

#include <chrono>
#include <gtest/gtest.h>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace ygg::tests
{

TEST(YggdrasilTests, CommonCoreUmbrellaExposesLightweightHelpers)
{
    EXPECT_EQ(ygg::to_ns(std::chrono::microseconds(1)), 1000);
    EXPECT_EQ(ygg::to_us(std::chrono::milliseconds(1)), 1000);
    EXPECT_EQ(ygg::to_ms(std::chrono::seconds(1)), 1000);
    EXPECT_EQ(ygg::to_uint_t(7), ygg::uint_t { 7 });
    EXPECT_THROW(ygg::to_uint_t(static_cast<size_t>(std::numeric_limits<ygg::uint_t>::max()) + 1), std::overflow_error);

    const auto interval = ygg::ClosedInterval<double>(1.0, 2.0);
    EXPECT_TRUE(contains(interval, 1.5));

    const auto prefix = std::filesystem::path("/tmp/yggdrasil");
    EXPECT_EQ(ygg::common::resolve_path(prefix, "relative"), prefix / "relative");
    EXPECT_EQ(ygg::common::resolve_path(prefix, "/absolute"), std::filesystem::path("/absolute"));

    static_assert(!std::is_copy_constructible_v<StopwatchScope<std::chrono::nanoseconds>>);
    static_assert(!std::is_move_constructible_v<StopwatchScope<std::chrono::nanoseconds>>);
}

#if !defined(__APPLE__)
TEST(YggdrasilTests, CommonCoreParsesLinuxPeakMemoryUsage)
{
    auto status = std::istringstream("Name:\tyggdrasil\nVmPeak:\t42 kB\n");
    EXPECT_EQ(ygg::detail::parse_linux_peak_memory_usage_in_kb(status), 42);

    auto missing = std::istringstream("Name:\tyggdrasil\nVmRSS:\t7 kB\n");
    EXPECT_EQ(ygg::detail::parse_linux_peak_memory_usage_in_kb(missing), -1);

    auto malformed = std::istringstream("VmPeak:\tnot-a-number kB\n");
    EXPECT_EQ(ygg::detail::parse_linux_peak_memory_usage_in_kb(malformed), -1);
}
#endif

}  // namespace ygg::tests
