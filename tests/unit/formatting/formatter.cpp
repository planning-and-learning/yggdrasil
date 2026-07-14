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

#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <yggdrasil/containers/associative_containers.hpp>
#include <yggdrasil/formatting/associative_container_formatters.hpp>
#include <yggdrasil/formatting/cista_formatters.hpp>
#include <yggdrasil/formatting/dynamic_bitset_formatters.hpp>
#include <yggdrasil/formatting/formatter.hpp>
#include <yggdrasil/semantics/comparators.hpp>
#include <yggdrasil/semantics/equal_to.hpp>
#include <yggdrasil/semantics/hash.hpp>

namespace ygg::tests
{

TEST(YggdrasilTests, CommonToStringUsesFmtFormatting) { EXPECT_EQ(ygg::to_string(42), "42"); }

TEST(YggdrasilTests, CommonToStringsFormatsRangeElements)
{
    const auto values = std::vector<int> { 1, 2, 3 };
    const auto strings = ygg::to_strings(values);

    EXPECT_EQ(strings, (std::vector<std::string> { "1", "2", "3" }));
}

TEST(YggdrasilTests, CommonToStringsAcceptsRangeAdaptors)
{
    const auto values = std::vector<int> { 1, 2, 3, 4 };
    auto even_squares = values | std::views::filter([](int value) { return value % 2 == 0; }) | std::views::transform([](int value) { return value * value; });

    EXPECT_EQ(ygg::to_strings(even_squares), (std::vector<std::string> { "4", "16" }));
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

TEST(YggdrasilTests, CommonAssociativeContainerFormatterFormatsFlatHashAliases)
{
    auto set = ygg::UnorderedSet<int> { 3, 1, 2, 10, 20 };
    EXPECT_EQ(fmt::format("{}", set), "{1, 10, 2, 20, 3}");

    auto map = ygg::UnorderedMap<int, std::string_view> { { 3, "three" }, { 1, "one" }, { 2, "two" }, { 10, "ten" }, { 20, "twenty" } };
    EXPECT_EQ(fmt::format("{}", map), "{1: one, 10: ten, 2: two, 20: twenty, 3: three}");
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
    EXPECT_EQ(fmt::format("{}", Variant {}), "<invalid>");
}

TEST(YggdrasilTests, CommonCistaFormatterFormatsViews)
{
    const auto context = 0;

    using Pair = ::cista::pair<int, ::cista::array<int, 2>>;
    using Array = ::cista::array<Pair, 2>;
    auto array = Array { Pair { 1, { 2, 3 } }, Pair { 4, { 5, 6 } } };
    using ArrayView = ygg::View<Array, int>;
    EXPECT_EQ(fmt::format("{}", ArrayView(array, context)), "[(1, [2, 3]), (4, [5, 6])]");

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
    EXPECT_EQ(fmt::format("{}", VariantView(Variant {}, context)), "<invalid>");
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
    const auto empty_blocks = std::vector<std::uint64_t> { 0 };
    const auto empty = ygg::BitsetSpan<const std::uint64_t>(empty_blocks.data(), 4);
    EXPECT_EQ(fmt::format("{}", empty), "{}");

    const auto blocks = std::vector<std::uint64_t> { 0b1010 };
    const auto value = ygg::BitsetSpan<const std::uint64_t>(blocks.data(), 4);

    EXPECT_EQ(fmt::format("{}", value), "{1, 3}");
}

}  // namespace ygg::tests
