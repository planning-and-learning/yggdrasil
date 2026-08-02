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
#include <chrono>
#include <cista/containers/vector.h>
#include <concepts>
#include <gtest/gtest.h>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#include <yggdrasil/core.hpp>
#include <yggdrasil/core/atomic_bit.hpp>
#include <yggdrasil/ids/index_mixins.hpp>

namespace ygg::tests
{

TEST(YggdrasilTests, CommonAtomicIntReferencePreservesAdjacentBits)
{
    auto blocks = std::array<uint8_t, 2> { 0b00111111, 0b11110000 };
    auto reference = ygg::bit::atomic_int_reference<uint8_t>(blocks.data(), 6, 6);

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

            ygg::bit::write_int(expected.data(), value, offset, len);
            ygg::bit::atomic_write_int(actual.data(), value, offset, len);

            EXPECT_EQ(actual, expected);
            EXPECT_EQ(ygg::bit::atomic_read_int(actual.data(), offset, len), ygg::bit::read_int(expected.data(), offset, len));
        }
    }
}

#ifndef NDEBUG
TEST(YggdrasilTests, CommonAtomicPackedAccessRejectsInvalidRanges)
{
    auto blocks = std::array<uint8_t, 2> {};
    constexpr auto digits = std::numeric_limits<uint8_t>::digits;

    EXPECT_DEATH((void) ygg::bit::atomic_read_int(blocks.data(), digits, 1), "Offset");
    EXPECT_DEATH(ygg::bit::atomic_write_int(blocks.data(), uint8_t { 0 }, 0, digits + 1), "Width");
    EXPECT_DEATH((void) ygg::bit::atomic_int_reference<uint8_t>(blocks.data(), digits, 1), "Offset");
}
#endif

struct CoreConceptFixture
{
    auto identifying_members() const noexcept { return 0; }
};

struct CoreTypeUtilsTag;

struct CoreTypeUtilsData
{
    CoreTypeUtilsData() = default;
    explicit CoreTypeUtilsData(int value_) : value(value_) {}

    int value {};
    auto identifying_members() const noexcept { return std::tie(value); }
};

}  // namespace ygg::tests

namespace ygg
{
template<>
struct Data<tests::CoreTypeUtilsTag> : tests::CoreTypeUtilsData
{
    using CoreTypeUtilsData::CoreTypeUtilsData;
};

template<>
struct Index<tests::CoreTypeUtilsTag> : IndexMixin<Index<tests::CoreTypeUtilsTag>>
{
    using Base = IndexMixin<Index<tests::CoreTypeUtilsTag>>;
    using Base::Base;
};
}  // namespace ygg

namespace ygg::tests
{

struct CoreTypeUtilsContext
{
    std::vector<ygg::Data<CoreTypeUtilsTag>> data;

    const ygg::Data<CoreTypeUtilsTag>& operator[](ygg::Index<CoreTypeUtilsTag> index) const { return data.at(index.get_value()); }
};

template<typename T>
using CoreAddConst = const T;

template<typename Bound, typename T>
using CorePair = std::pair<Bound, T>;

TEST(YggdrasilTests, CommonCoreUmbrellaExposesLightweightHelpers)
{
    EXPECT_EQ(ygg::bit::ceil_div(0u, 2u), 0u);
    EXPECT_EQ(ygg::bit::ceil_div(5u, 2u), 3u);
    EXPECT_EQ(ygg::bit::ceil_div(std::numeric_limits<unsigned>::max(), 2u), std::numeric_limits<unsigned>::max() / 2u + 1u);
    auto packed_blocks = std::array<uint8_t, 2> {};
    ygg::bit::write_int(packed_blocks.data(), uint8_t { 0b111111 }, 6, 6);
    EXPECT_EQ(ygg::bit::read_int(packed_blocks.data(), 6, 6), uint8_t { 0b111111 });

    auto packed_ref = ygg::bit::int_reference<uint8_t>(packed_blocks.data(), 0, 3);
    packed_ref = uint8_t { 5 };
    EXPECT_EQ(static_cast<uint8_t>(packed_ref), uint8_t { 5 });
    EXPECT_THROW((packed_ref = uint8_t { 8 }), std::out_of_range);

    EXPECT_EQ(ygg::to_ns(std::chrono::microseconds(1)), 1000);
    EXPECT_EQ(ygg::to_us(std::chrono::milliseconds(1)), 1000);
    EXPECT_EQ(ygg::to_ms(std::chrono::seconds(1)), 1000);
    EXPECT_EQ(ygg::to_uint_t(7), ygg::uint_t { 7 });
    EXPECT_THROW(ygg::to_uint_t(static_cast<size_t>(std::numeric_limits<ygg::uint_t>::max()) + 1), std::overflow_error);

    int value = 1;
    auto observer = ygg::make_observer(value);
    static_assert(std::is_same_v<decltype(observer), ygg::ObserverPtr<int>>);
    EXPECT_EQ(*observer, 1);
    *observer = 2;
    EXPECT_EQ(value, 2);

    const int const_value = 3;
    auto const_observer = ygg::make_observer(const_value);
    static_assert(std::is_same_v<decltype(const_observer), ygg::ObserverPtr<const int>>);
    EXPECT_EQ(*const_observer, 3);
    EXPECT_EQ(ygg::Hash<ygg::ObserverPtr<const int>> {}(const_observer), ygg::Hash<int> {}(3));
    EXPECT_TRUE(ygg::EqualTo<ygg::ObserverPtr<const int>> {}(const_observer, ygg::make_observer(const_value)));

    const auto interval = ygg::ClosedInterval<double>(1.0, 2.0);
    EXPECT_TRUE(contains(interval, 1.5));

    const auto prefix = std::filesystem::path("/tmp/yggdrasil");
    EXPECT_EQ(ygg::common::resolve_path(prefix, "relative"), prefix / "relative");
    EXPECT_EQ(ygg::common::resolve_path(prefix, "/absolute"), std::filesystem::path("/absolute"));

    auto optional = ::cista::optional<int> { 7 };
    ygg::clear(optional);
    EXPECT_FALSE(optional.has_value());

    static_assert(Identifiable<CoreConceptFixture>);
    static_assert(InputRangeOf<std::vector<int>, int>);
    static_assert(TriviallyCopyable<int>);

    using Tuple = TypeListToTupleT<TypeList<int, double>>;
    static_assert(std::is_same_v<Tuple, std::tuple<int, double>>);
    static_assert(std::is_same_v<ApplyTypeListT<std::tuple, TypeList<int, double>>, std::tuple<int, double>>);
    static_assert(std::is_same_v<MapTypeListT<CoreAddConst, TypeList<int, double>>, TypeList<const int, const double>>);
    static_assert(std::is_same_v<MapTypeListSecondT<CorePair, int, TypeList<float, double>>, TypeList<std::pair<int, float>, std::pair<int, double>>>);
    static_assert(std::is_same_v<ConcatTypeListsT<TypeList<int>, TypeList<float, double>, TypeList<char>>, TypeList<int, float, double, char>>);

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

TEST(YggdrasilTests, CommonCoreObserverPtrExposesNonOwningValueSemantics)
{
    static_assert(!std::is_convertible_v<ygg::ObserverPtr<int>, int*>);
    static_assert(!std::equality_comparable<ygg::ObserverPtr<int>>);
    static_assert(!std::totally_ordered<ygg::ObserverPtr<int>>);

    const auto empty = ygg::ObserverPtr<int>();
    EXPECT_EQ(empty.get(), nullptr);
    EXPECT_FALSE(empty);

    auto first = 1;
    auto also_first = 1;
    auto second = 2;
    auto first_observer = ygg::make_observer(first);
    const auto also_first_observer = ygg::make_observer(also_first);
    const auto second_observer = ygg::make_observer(second);

    EXPECT_EQ(first_observer.get(), &first);
    EXPECT_TRUE(first_observer);
    EXPECT_EQ(*first_observer, 1);
    *first_observer = 3;
    EXPECT_EQ(first, 3);

    first = 1;
    EXPECT_TRUE(ygg::EqualTo<ygg::ObserverPtr<int>> {}(first_observer, also_first_observer));
    EXPECT_FALSE(ygg::EqualTo<ygg::ObserverPtr<int>> {}(first_observer, second_observer));
    EXPECT_TRUE(ygg::Less<ygg::ObserverPtr<int>> {}(first_observer, second_observer));
    EXPECT_EQ(ygg::Hash<ygg::ObserverPtr<int>> {}(first_observer), ygg::Hash<int> {}(first));
}

TEST(YggdrasilTests, CommonCoreTypeUtilsExtendsAnyInputRange)
{
    auto context = CoreTypeUtilsContext { { ygg::Data<CoreTypeUtilsTag> { 1 }, ygg::Data<CoreTypeUtilsTag> { 2 } } };
    const auto first = ygg::View<ygg::Index<CoreTypeUtilsTag>, CoreTypeUtilsContext>(ygg::Index<CoreTypeUtilsTag>(0), context);
    const auto second = ygg::View<ygg::Index<CoreTypeUtilsTag>, CoreTypeUtilsContext>(ygg::Index<CoreTypeUtilsTag>(1), context);
    const auto views = std::array { first, second };

    auto indices = ygg::IndexList<CoreTypeUtilsTag> {};
    ygg::extend(views, indices);

    ASSERT_EQ(indices.size(), 2);
    EXPECT_EQ(indices[0], ygg::Index<CoreTypeUtilsTag>(0));
    EXPECT_EQ(indices[1], ygg::Index<CoreTypeUtilsTag>(1));

    indices.push_back(ygg::Index<CoreTypeUtilsTag>(0));
    ygg::set(views, indices);

    ASSERT_EQ(indices.size(), 2);
    EXPECT_EQ(indices[0], ygg::Index<CoreTypeUtilsTag>(0));
    EXPECT_EQ(indices[1], ygg::Index<CoreTypeUtilsTag>(1));
}

TEST(YggdrasilTests, CommonCoreCartesianSetResetsWorkspaceOnEarlyReturns)
{
    auto workspace = ygg::itertools::cartesian_set::Workspace<int> {};
    auto first_ranges = std::vector<std::vector<int>> { { 1, 2 }, { 3 } };
    auto elements = std::vector<std::vector<int>> {};

    ygg::itertools::cartesian_set::for_each_element(first_ranges.begin(),
                                                    first_ranges.end(),
                                                    workspace,
                                                    [&](const auto& element) { elements.push_back(element); });

    EXPECT_EQ(elements, (std::vector<std::vector<int>> { { 1, 3 }, { 2, 3 } }));
    EXPECT_EQ(workspace.indices.size(), 2);

    auto range_with_empty_inner = std::vector<std::vector<int>> { { 1 }, {} };
    ygg::itertools::cartesian_set::for_each_element(range_with_empty_inner.begin(), range_with_empty_inner.end(), workspace, [&](const auto&) { FAIL(); });

    EXPECT_EQ(workspace.element.size(), 2);
    EXPECT_EQ(workspace.indices, (std::vector<size_t> { 0, 0 }));

    auto empty_outer = std::vector<std::vector<int>> {};
    auto saw_empty_element = false;
    ygg::itertools::cartesian_set::for_each_element(empty_outer.begin(),
                                                    empty_outer.end(),
                                                    workspace,
                                                    [&](const auto& element)
                                                    {
                                                        saw_empty_element = true;
                                                        EXPECT_TRUE(element.empty());
                                                    });

    EXPECT_TRUE(saw_empty_element);
    EXPECT_TRUE(workspace.element.empty());
    EXPECT_TRUE(workspace.indices.empty());
}

}  // namespace ygg::tests
