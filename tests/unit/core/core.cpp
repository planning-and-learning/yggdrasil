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
#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>

#include <urd/repr/containers/bit.hpp>
#include <urd/elements/flat.hpp>
#include "yggdrasil/core/config.hpp"

#include <array>
#include <chrono>
#include <cista/containers/string.h>
#include <cista/containers/variant.h>
#include <cista/containers/vector.h>
#include <concepts>
#include <gtest/gtest.h>
#include <limits>
#include <ranges>
#include <span>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace ygg::tests
{

TEST(YggdrasilTests, CommonAtomicIntReferencePreservesAdjacentBits)
{
    auto blocks = std::array<uint8_t, 2> { 0b00111111, 0b11110000 };
    auto reference = urd::repr::bit::atomic_int_reference<uint8_t>(blocks, 6, 6);

    reference = uint8_t { 0b101011 };

    EXPECT_EQ(static_cast<uint8_t>(reference), uint8_t { 0b101011 });
    EXPECT_EQ(blocks, (std::array<uint8_t, 2> { 0b11111111, 0b11111010 }));
    EXPECT_THROW((reference = uint8_t { 0b1000000 }), std::out_of_range);
}

TEST(YggdrasilTests, CommonAtomicPackedAccessMatchesNonAtomicAccess)
{
    for (uint8_t offset = 0; offset < std::numeric_limits<uint8_t>::digits; ++offset)
    {
        for (uint8_t len = 0; len <= std::numeric_limits<uint8_t>::digits; ++len)
        {
            auto expected = std::array<uint8_t, 2> { 0b10100101, 0b01011010 };
            auto actual = expected;
            constexpr auto value = uint8_t { 0b11010011 };

            urd::repr::bit::write_int<uint8_t>(expected, value, offset, len);
            urd::repr::bit::atomic_write_int<uint8_t>(actual, value, offset, len);

            EXPECT_EQ(actual, expected);
            EXPECT_EQ(urd::repr::bit::atomic_read_int<uint8_t>(actual, offset, len), urd::repr::bit::read_int<uint8_t>(expected, offset, len));
        }
    }
}

#ifndef NDEBUG
TEST(YggdrasilTests, CommonAtomicPackedAccessRejectsInvalidRanges)
{
    auto blocks = std::array<uint8_t, 2> {};
    constexpr auto digits = std::numeric_limits<uint8_t>::digits;

    EXPECT_DEATH((void) urd::repr::bit::atomic_read_int<uint8_t>(blocks, digits, 1), "Offset");
    EXPECT_DEATH(urd::repr::bit::atomic_write_int<uint8_t>(blocks, uint8_t { 0 }, 0, digits + 1), "Width");
    EXPECT_DEATH((void) urd::repr::bit::atomic_int_reference<uint8_t>(blocks, digits, 1), "Offset");
}
#endif

namespace
{
struct CoreConceptFixture
{
    static constexpr auto identifying_members() noexcept { return urd::Members<> {}; }
};
}  // namespace

struct CoreTypeUtilsTag;

namespace
{
struct CoreTypeUtilsData
{
    CoreTypeUtilsData() = default;
    explicit CoreTypeUtilsData(int value_) : value(value_) {}

    int value {};
    static constexpr auto cista_members() noexcept { return urd::Members<&CoreTypeUtilsData::value> {}; }
    static constexpr auto identifying_members() noexcept { return urd::Members<&CoreTypeUtilsData::value> {}; }
};
}  // namespace

}  // namespace ygg::tests

namespace urd
{
template<>
struct data<ygg::tests::CoreTypeUtilsTag> : ygg::tests::CoreTypeUtilsData
{
    using CoreTypeUtilsData::CoreTypeUtilsData;
};

}  // namespace urd

namespace ygg::tests
{

namespace
{
struct CoreTypeUtilsContext
{
    urd::Store<urd::sequence<urd::data<CoreTypeUtilsTag>>> data;

    const urd::data<CoreTypeUtilsTag>& operator[](urd::index<CoreTypeUtilsTag> index) const { return data.at(index.value); }
};
}  // namespace

TEST(YggdrasilTests, CommonCoreUmbrellaExposesLightweightHelpers)
{
    EXPECT_EQ(urd::repr::bit::ceil_div(0u, 2u), 0u);
    EXPECT_EQ(urd::repr::bit::ceil_div(5u, 2u), 3u);
    EXPECT_EQ(urd::repr::bit::ceil_div(std::numeric_limits<unsigned>::max(), 2u), std::numeric_limits<unsigned>::max() / 2u + 1u);
    auto packed_blocks = std::array<uint8_t, 2> {};
    urd::repr::bit::write_int<uint8_t>(packed_blocks, uint8_t { 0b111111 }, 6, 6);
    EXPECT_EQ(urd::repr::bit::read_int<uint8_t>(packed_blocks, 6, 6), uint8_t { 0b111111 });

    auto packed_ref = urd::repr::bit::int_reference<uint8_t>(packed_blocks, 0, 3);
    packed_ref = uint8_t { 5 };
    EXPECT_EQ(static_cast<uint8_t>(packed_ref), uint8_t { 5 });
    EXPECT_THROW((packed_ref = uint8_t { 8 }), std::out_of_range);

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

    static_assert(urd::Identifiable<CoreConceptFixture>);

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

TEST(YggdrasilTests, CommonCoreViewsGiveTheirStoredForms)
{
    auto context = CoreTypeUtilsContext { { urd::data<CoreTypeUtilsTag> { 1 }, urd::data<CoreTypeUtilsTag> { 2 } } };
    const auto first = urd::view<CoreTypeUtilsTag, CoreTypeUtilsContext>(urd::index<CoreTypeUtilsTag>(0), context);
    const auto second = urd::view<CoreTypeUtilsTag, CoreTypeUtilsContext>(urd::index<CoreTypeUtilsTag>(1), context);

    // An index view as its index, a range of them as the flat vector of their indices.
    EXPECT_EQ(urd::to_stored(second), urd::index<CoreTypeUtilsTag>(1));
    const auto views = std::array { first, second };
    const auto values = views | std::views::transform([](auto view) { return view; });
    EXPECT_EQ(urd::to_stored(values), (urd::flat_vector<urd::index<CoreTypeUtilsTag>> { urd::index<CoreTypeUtilsTag>(0), urd::index<CoreTypeUtilsTag>(1) }));

    // A data view as a copy of its record.
    const auto record = urd::data<CoreTypeUtilsTag> { 3 };
    const auto data = urd::borrowed_view<CoreTypeUtilsTag, CoreTypeUtilsContext>(record, context);
    EXPECT_EQ(urd::to_stored(data).value, 3);
}

}  // namespace ygg::tests
