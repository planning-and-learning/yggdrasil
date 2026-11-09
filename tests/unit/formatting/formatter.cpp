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

#include "yggdrasil/formatting/formatter.hpp"

#include "yggdrasil/formatting/cista_formatters.hpp"
#include "yggdrasil/formatting/dynamic_bitset_formatters.hpp"

#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <urd/elements/equal_to.hpp>
#include <urd/elements/hash.hpp>
#include <urd/elements/less.hpp>
#include <urd/repr/containers/bitset_span.hpp>
#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>
#include <variant>
#include <vector>

namespace ygg::tests
{

TEST(YggdrasilTests, CommonToStringUsesFmtFormatting) { EXPECT_EQ(ygg::to_string(42), "42"); }

TEST(YggdrasilTests, CommonToStringsFormatsRangeElements)
{
    const auto values = urd::Store<urd::sequence<int>> { 1, 2, 3 };
    const auto strings = ygg::to_strings(values);

    EXPECT_EQ(strings, (urd::Store<urd::sequence<std::string>> { "1", "2", "3" }));
}

TEST(YggdrasilTests, CommonToStringsAcceptsRangeAdaptors)
{
    const auto values = urd::Store<urd::sequence<int>> { 1, 2, 3, 4 };
    auto even_squares = values | std::views::filter([](int value) { return value % 2 == 0; }) | std::views::transform([](int value) { return value * value; });

    EXPECT_EQ(ygg::to_strings(even_squares), (urd::Store<urd::sequence<std::string>> { "4", "16" }));
}

TEST(YggdrasilTests, CommonFormatterHandlesNullableWrappers)
{
    EXPECT_EQ(fmt::format("{}", std::optional<int> {}), "<nullopt>");
    EXPECT_EQ(fmt::format("{}", std::optional<int> { 7 }), "7");
    EXPECT_EQ(fmt::format("{}", std::shared_ptr<int> {}), "<nullptr>");
    EXPECT_EQ(fmt::format("{}", std::make_shared<int>(9)), "9");
    EXPECT_EQ(fmt::format("{}", std::unique_ptr<int> {}), "<nullptr>");
    EXPECT_EQ(fmt::format("{}", std::make_unique<int>(11)), "11");
    EXPECT_EQ(fmt::format("{}", std::monostate {}), "monostate");
}

TEST(YggdrasilTests, CommonCistaFormatterFormatsOffsetString)
{
    auto value = urd::flat_string {};
    value = "hello";

    EXPECT_EQ(fmt::format("{}", value), "hello");
}

TEST(YggdrasilTests, CommonCistaFormatterFormatsOptionalVectorAndVariant)
{
    auto empty = urd::flat_optional<int> {};
    auto optional = urd::flat_optional<int> { 7 };
    EXPECT_EQ(fmt::format("{}", empty), "<nullopt>");
    EXPECT_EQ(fmt::format("{}", optional), "7");

    auto vector = urd::flat_vector<int> {};
    vector.emplace_back(1);
    vector.emplace_back(2);
    EXPECT_EQ(fmt::format("{}", vector), "[1, 2]");

    using Variant = urd::flat_variant<int, unsigned>;
    auto variant = Variant { 9U };
    EXPECT_EQ(fmt::format("{}", variant), "9");
    EXPECT_EQ(fmt::format("{}", Variant {}), "<invalid>");
}

TEST(YggdrasilTests, CommonDynamicBitsetFormatterFormatsBoostDynamicBitset)
{
    auto empty = boost::dynamic_bitset<>(8);
    EXPECT_EQ(fmt::format("{}", empty), "{}");

    auto value = boost::dynamic_bitset<>(8);
    value.set(1);
    value.set(3);

    EXPECT_EQ(fmt::format("{}", value), "{1, 3}");
}

TEST(YggdrasilTests, CommonDynamicBitsetFormatterFormatsBitsetSpan)
{
    const auto empty_blocks = urd::Store<urd::sequence<std::uint64_t>, urd::contiguous> { 0 };
    const auto empty = urd::repr::BitsetSpan<const std::uint64_t>(empty_blocks.data(), 4);
    EXPECT_EQ(fmt::format("{}", empty), "{}");

    const auto blocks = urd::Store<urd::sequence<std::uint64_t>, urd::contiguous> { 0b1010 };
    const auto value = urd::repr::BitsetSpan<const std::uint64_t>(blocks.data(), 4);

    EXPECT_EQ(fmt::format("{}", value), "{1, 3}");
}

}  // namespace ygg::tests
