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

#include <concepts>
#include <gtest/gtest.h>
#include <yggdrasil/containers/vector.hpp>
#include <yggdrasil/core/config.hpp>

namespace ygg::tests
{

TEST(YggdrasilTests, CommonCistaVectorViewExposesBackAndRandomAccessIterators)
{
    const auto context = 0;
    auto vector = ::cista::offset::vector<int> {};
    vector.emplace_back(1);
    vector.emplace_back(2);
    vector.emplace_back(3);

    using Vector = decltype(vector);
    const auto view = ygg::View<Vector, int>(vector, context);

    static_assert(std::same_as<decltype(view.get_handle()), const Vector&>);
    EXPECT_EQ(view.get_handle().size(), 3);
    EXPECT_EQ(view.front(), 1);
    EXPECT_EQ(view.back(), 3);
    EXPECT_EQ(view[1], 2);
    EXPECT_EQ(*view.begin(), 1);
    EXPECT_EQ(view.begin()[2], 3);
    EXPECT_EQ(view.end() - view.begin(), 3);
    EXPECT_LT(view.begin(), view.end());
}

TEST(YggdrasilTests, CommonVector)
{
    const auto dim0 = 2;
    const auto dim1 = 3;
    const auto dim2 = 4;
    auto vec = std::vector<uint64_t>(2 * 3 * 4, uint64_t(0));

    auto mdspan = ygg::MDSpan<uint64_t, 3>(vec.data(), std::array<size_t, 3> { dim0, dim1, dim2 });

    EXPECT_EQ(mdspan.size(), 24);
    EXPECT_EQ(mdspan.shapes(), (std::array<size_t, 3> { dim0, dim1, dim2 }));
    EXPECT_EQ(mdspan.strides(), (std::array<size_t, 3> { 12, 4, 1 }));
    EXPECT_EQ(mdspan.stride(), mdspan.strides());
    EXPECT_EQ(mdspan.begin(), vec.data());
    EXPECT_EQ(mdspan.cbegin(), vec.data());
    EXPECT_EQ(mdspan.end(), vec.data() + vec.size());
    EXPECT_EQ(mdspan.cend(), vec.data() + vec.size());

    auto submdspan_full = mdspan();
    auto checked_submdspan_full = mdspan.at();
    EXPECT_EQ(submdspan_full.size(), 24);
    EXPECT_EQ(checked_submdspan_full.size(), 24);

    auto submdspan_0 = mdspan(0);
    EXPECT_EQ(submdspan_0.size(), 12);

    auto submdspan_1 = mdspan(1);
    EXPECT_EQ(submdspan_1.size(), 12);

    auto submdspan_0_0 = mdspan(0, 0);
    EXPECT_EQ(submdspan_0_0.size(), 4);

    auto submdspan_0_1 = mdspan(0, 1);
    EXPECT_EQ(submdspan_0_1.size(), 4);

    auto submdspan_0_2 = mdspan(0, 2);
    EXPECT_EQ(submdspan_0_2.size(), 4);

    submdspan_0_2(0) = 5;
    mdspan.at(1, 1, 1) = 9;
    EXPECT_EQ(submdspan_0_2(0), 5);
    EXPECT_EQ(submdspan_0_2.at(0), 5);
    EXPECT_EQ(submdspan_0(2, 0), 5);
    EXPECT_EQ(mdspan(0, 2, 0), 5);
    EXPECT_EQ(mdspan.at(0).at(2).at(0), 5);
    EXPECT_EQ(mdspan.at(1, 1, 1), 9);
    EXPECT_EQ(mdspan.at(1, 1).at(1), 9);
    EXPECT_THROW(mdspan.at(2), std::out_of_range);
    EXPECT_THROW(mdspan.at(0, 3), std::out_of_range);
    EXPECT_THROW(mdspan.at(0, 0, 4), std::out_of_range);

    EXPECT_EQ(submdspan_0_2.data(), &mdspan(0, 2, 0));
    EXPECT_EQ(submdspan_1.data(), &mdspan(1, 0, 0));

    const auto& const_mdspan = mdspan;
    EXPECT_EQ(const_mdspan.end(), vec.data() + vec.size());
    EXPECT_EQ(const_mdspan(0, 2, 0), 5);
    EXPECT_EQ(const_mdspan.at(0, 2, 0), 5);
    EXPECT_EQ(const_mdspan[0][2][0], 5);
    EXPECT_EQ(const_mdspan.at(0).at(2).at(0), 5);
    EXPECT_EQ(const_mdspan(0, 2).data(), &const_mdspan(0, 2, 0));
    EXPECT_EQ(const_mdspan.at(0, 2).data(), &const_mdspan(0, 2, 0));
    EXPECT_THROW(const_mdspan.at(2), std::out_of_range);
}

}  // namespace ygg::tests
