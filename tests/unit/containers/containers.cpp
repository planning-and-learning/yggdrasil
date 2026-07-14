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
#include <cstddef>
#include <gtest/gtest.h>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <yggdrasil/containers/containers.hpp>

namespace ygg::tests
{

TEST(YggdrasilTests, CommonArrayViewsExposeHandlesAndRandomAccessIterators)
{
    const auto context = 0;

    auto block_storage = std::array<unsigned, 2> { 1, 2 };
    using BlockView = ygg::BasicBlockArrayView<unsigned, ygg::bit::ForwardingBlockCoder<unsigned>>;
    const auto block_data = BlockView(block_storage.data(), block_storage.size());
    const auto block_view = ygg::View<BlockView, int>(block_data, context);

    static_assert(std::same_as<decltype(block_view.get_handle()), const BlockView&>);
    EXPECT_EQ(block_view.get_handle().size(), 2);
    static_assert(!noexcept(block_view.front()));
    static_assert(!noexcept(block_view.back()));
    EXPECT_EQ(block_view.front(), 1U);
    EXPECT_EQ(block_view.back(), 2U);
    EXPECT_EQ(block_view[1], 2U);
    EXPECT_EQ(*block_view.begin(), 1U);
    EXPECT_EQ(block_view.begin()[1], 2U);
    EXPECT_EQ(block_view.end() - block_view.begin(), 2);
    EXPECT_LT(block_view.begin(), block_view.end());

    auto bit_storage = std::array<unsigned, 1> { 0 };
    auto bit_data = ygg::BasicBitPackedArrayView<unsigned, ygg::bit::ForwardingBlockCoder<unsigned>>(bit_storage.data(), 2, 2, 0);
    const auto bit_values = std::array<unsigned, 2> { 1, 2 };
    bit_data = std::span<const unsigned>(bit_values);
    using BitPackedView = decltype(bit_data);
    const auto bit_view = ygg::View<BitPackedView, int>(bit_data, context);

    static_assert(std::same_as<decltype(bit_view.get_handle()), const BitPackedView&>);
    EXPECT_EQ(bit_view.get_handle().size(), 2);
    static_assert(!noexcept(bit_view.front()));
    static_assert(!noexcept(bit_view.back()));
    EXPECT_EQ(bit_view.front(), 1U);
    EXPECT_EQ(bit_view.back(), 2U);
    EXPECT_EQ(bit_view[1], 2U);
    EXPECT_EQ(*bit_view.begin(), 1U);
    EXPECT_EQ(bit_view.begin()[1], 2U);
    EXPECT_EQ(bit_view.end() - bit_view.begin(), 2);
    EXPECT_LT(bit_view.begin(), bit_view.end());
}

TEST(YggdrasilTests, CommonArrayViewFrontBackPropagateEmptyViewErrors)
{
    const auto context = 0;

    auto block_storage = std::array<unsigned, 1> { 0 };
    using BlockView = ygg::BasicBlockArrayView<unsigned, ygg::bit::ForwardingBlockCoder<unsigned>>;
    const auto empty_block_data = BlockView(block_storage.data(), 0);
    const auto empty_block_view = ygg::View<BlockView, int>(empty_block_data, context);

    EXPECT_THROW(empty_block_view.front(), std::out_of_range);
    EXPECT_THROW(empty_block_view.back(), std::out_of_range);

    auto bit_storage = std::array<unsigned, 1> { 0 };
    using BitPackedView = ygg::BasicBitPackedArrayView<unsigned, ygg::bit::ForwardingBlockCoder<unsigned>>;
    const auto empty_bit_data = BitPackedView(bit_storage.data(), 0, 2, 0);
    const auto empty_bit_view = ygg::View<BitPackedView, int>(empty_bit_data, context);

    EXPECT_THROW(empty_bit_view.front(), std::out_of_range);
    EXPECT_THROW(empty_bit_view.back(), std::out_of_range);
}

TEST(YggdrasilTests, CistaArrayAndPairViewsCompose)
{
    using InnerArray = ::cista::array<int, 2>;
    using Pair = ::cista::pair<int, InnerArray>;
    using Array = ::cista::array<Pair, 2>;

    static_assert(ygg::ViewConcept<InnerArray, int>);
    static_assert(ygg::ViewConcept<Pair, int>);
    static_assert(ygg::ViewConcept<Array, int>);

    const auto context = 0;
    const auto data = Array { Pair { 1, InnerArray { 2, 3 } }, Pair { 4, InnerArray { 5, 6 } } };
    const auto view = ygg::View<Array, int>(data, context);

    static_assert(std::same_as<decltype(view.get_handle()), const Array&>);
    static_assert(std::same_as<decltype(view[0].get_first()), const int&>);
    static_assert(std::same_as<decltype(view[0].get_second()[0]), const int&>);
    static_assert(std::random_access_iterator<typename decltype(view)::const_iterator>);
    EXPECT_EQ(view.size(), 2);
    EXPECT_FALSE(view.empty());
    EXPECT_EQ(&view.get_context(), &context);
    EXPECT_EQ(&view[0].get_context(), &context);
    EXPECT_EQ(&view[0].get_second().get_context(), &context);
    EXPECT_EQ(view.front().get_first(), 1);
    EXPECT_EQ(view.back().get_second().back(), 6);
    EXPECT_EQ(view[1].get_second()[0], 5);
    EXPECT_EQ((*view.begin()).get_first(), 1);
    EXPECT_EQ(view.begin()[1].get_second().front(), 5);
    EXPECT_EQ(view.end() - view.begin(), 2);

    const auto empty_data = ::cista::array<Pair, 0> {};
    const auto empty_view = ygg::View<::cista::array<Pair, 0>, int>(empty_data, context);
    EXPECT_TRUE(empty_view.empty());
    EXPECT_EQ(empty_view.begin(), empty_view.end());
}

TEST(YggdrasilTests, CommonContainersUmbrellaHeaderCompiles)
{
    std::array<unsigned, 1> blocks { 0 };
    ygg::BitsetSpan<unsigned> bits(blocks.data(), 8);
    bits[3] = true;
    EXPECT_TRUE(bits.test(3));

    std::array<int, 3> values { 1, 2, 3 };
    const std::array<size_t, 1> shape { values.size() };
    ygg::MDSpan<int, 1> span(values.data(), shape);
    EXPECT_EQ(span[2], 3);

    std::tuple<int, int> tuple { 1, 2 };
    int visited_value = 0;
    EXPECT_TRUE(ygg::visit_at(tuple, 1, [&](const auto& value) { visited_value = value; }));
    EXPECT_EQ(visited_value, 2);

    EXPECT_FALSE(ygg::visit_at(tuple, 2, [&](const auto&) { visited_value = 9; }));
    EXPECT_EQ(visited_value, 2);

    const auto const_tuple = std::tuple<int, int> { 3, 4 };
    EXPECT_TRUE(ygg::visit_at(const_tuple, 0, [&](const auto& value) { visited_value = value; }));
    EXPECT_EQ(visited_value, 3);

    EXPECT_FALSE(ygg::visit_at(std::tuple<> {}, 0, [](const auto&) {}));

    using BlockView = ygg::BasicBlockArrayView<unsigned, ygg::bit::ForwardingBlockCoder<unsigned>>;
    using BitPackedView = ygg::BasicBitPackedArrayView<unsigned, ygg::bit::ForwardingBlockCoder<unsigned>>;
    static_assert(ygg::ViewConcept<::cista::optional<int>, int>);
    static_assert(ygg::ViewConcept<::cista::offset::vector<int>, int>);
    static_assert(ygg::ViewConcept<::cista::offset::variant<int, unsigned>, int>);
    static_assert(ygg::ViewConcept<BlockView, int>);
    static_assert(ygg::ViewConcept<BitPackedView, int>);

    const auto context = 0;
    auto optional = ::cista::optional<int> { 7 };
    auto optional_view = ygg::View<::cista::optional<int>, int>(optional, context);
    static_assert(!noexcept(*optional_view));
    EXPECT_TRUE(optional_view);
    EXPECT_TRUE(optional_view.has_value());
    EXPECT_EQ(optional_view.value(), 7);
    EXPECT_EQ(*optional_view, 7);
    EXPECT_EQ(*optional_view.operator->(), 7);

    using Variant = ::cista::offset::variant<int, unsigned>;
    auto invalid_variant = Variant {};
    const auto invalid_variant_view = ygg::View<Variant, int>(invalid_variant, context);
    EXPECT_FALSE(invalid_variant_view);
    EXPECT_FALSE(invalid_variant_view.valid());

    auto variant = Variant { 7U };
    const auto variant_view = ygg::View<Variant, int>(variant, context);
    EXPECT_TRUE(variant_view);
    EXPECT_TRUE(variant_view.valid());
    EXPECT_TRUE(variant_view.is<unsigned>());
    EXPECT_EQ(variant_view.get<unsigned>(), 7U);
}

}  // namespace ygg::tests
