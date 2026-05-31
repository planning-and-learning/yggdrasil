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

#include <gtest/gtest.h>
#include <yggdrasil/formatting/associative_container_formatters.hpp>
#include <yggdrasil/containers/associative_containers.hpp>
#include <yggdrasil/semantics/comparators.hpp>
#include <yggdrasil/formatting/cista_formatters.hpp>
#include <yggdrasil/formatting/dynamic_bitset_formatters.hpp>
#include <yggdrasil/formatting/formatter.hpp>
#include <yggdrasil/semantics/equal_to.hpp>
#include <yggdrasil/semantics/hash.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ygg::tests
{

TEST(YggdrasilTests, CommonToStringUsesFmtFormatting)
{
    EXPECT_EQ(ygg::to_string(42), "42");
}

TEST(YggdrasilTests, CommonToStringsFormatsRangeElements)
{
    const auto values = std::vector<int> { 1, 2, 3 };
    const auto strings = ygg::to_strings(values);

    EXPECT_EQ(strings, (std::vector<std::string> { "1", "2", "3" }));
}

TEST(YggdrasilTests, CommonFormatterHandlesNullableWrappers)
{
    EXPECT_EQ(fmt::format("{}", std::optional<int> {}), "<nullopt>");
    EXPECT_EQ(fmt::format("{}", std::optional<int> { 7 }), "7");
    EXPECT_EQ(fmt::format("{}", std::shared_ptr<int> {}), "<nullptr>");
    EXPECT_EQ(fmt::format("{}", std::make_shared<int>(9)), "9");
}

TEST(YggdrasilTests, CommonAssociativeContainerFormatterFormatsFlatHashMap)
{
    auto values = ygg::UnorderedMap<int, std::string_view> {};
    values.emplace(1, "one");

    EXPECT_EQ(fmt::format("{}", values), "{1: one}");
}

TEST(YggdrasilTests, CommonFormatterFormatsOrderedAssociativeAliases)
{
    const auto set = ygg::Set<int> { 1, 2 };
    EXPECT_EQ(fmt::format("{}", set), "{1, 2}");

    const auto map = ygg::Map<int, std::string_view> { { 1, "one" }, { 2, "two" } };
    EXPECT_EQ(fmt::format("{}", map), "{1: one, 2: two}");
}

TEST(YggdrasilTests, CommonCistaFormatterFormatsOffsetString)
{
    auto value = ::cista::offset::string {};
    value = "hello";

    EXPECT_EQ(fmt::format("{}", value), "hello");
}

TEST(YggdrasilTests, CommonCistaFormatterFormatsOptionalVectorAndVariant)
{
    auto empty = ::cista::optional<int> {};
    auto optional = ::cista::optional<int> { 7 };
    EXPECT_EQ(fmt::format("{}", empty), "<nullopt>");
    EXPECT_EQ(fmt::format("{}", optional), "7");

    auto vector = ::cista::offset::vector<int> {};
    vector.emplace_back(1);
    vector.emplace_back(2);
    EXPECT_EQ(fmt::format("{}", vector), "[1, 2]");

    using Variant = ::cista::offset::variant<int, unsigned>;
    auto variant = Variant { 9U };
    EXPECT_EQ(fmt::format("{}", variant), "9");
}

TEST(YggdrasilTests, CommonCistaFormatterFormatsViews)
{
    const auto context = 0;

    auto vector = ::cista::offset::vector<int> {};
    vector.emplace_back(1);
    vector.emplace_back(2);
    using VectorView = ygg::View<decltype(vector), int>;
    EXPECT_EQ(fmt::format("{}", VectorView(vector, context)), "[1, 2]");

    auto optional = ::cista::optional<int> { 7 };
    using OptionalView = ygg::View<decltype(optional), int>;
    EXPECT_EQ(fmt::format("{}", OptionalView(optional, context)), "7");

    using Variant = ::cista::offset::variant<int, unsigned>;
    auto variant = Variant { 9U };
    using VariantView = ygg::View<Variant, int>;
    EXPECT_EQ(fmt::format("{}", VariantView(variant, context)), "9");
}

TEST(YggdrasilTests, CommonDynamicBitsetFormatterFormatsBoostDynamicBitset)
{
    auto value = boost::dynamic_bitset<>(8);
    value.set(1);
    value.set(3);

    EXPECT_EQ(fmt::format("{}", value), "{1, 3}");
}

TEST(YggdrasilTests, CommonDynamicBitsetFormatterFormatsBitsetSpan)
{
    const auto blocks = std::vector<std::uint64_t> { 0b1010 };
    const auto value = ygg::BitsetSpan<const std::uint64_t>(blocks.data(), 4);

    EXPECT_EQ(fmt::format("{}", value), "{1, 3}");
}

}
