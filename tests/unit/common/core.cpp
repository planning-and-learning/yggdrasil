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
#include <yggdrasil/core.hpp>

#include <chrono>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace ygg::tests
{

struct CoreConceptFixture
{
    auto identifying_members() const noexcept { return 0; }
};

TEST(YggdrasilTests, CommonCoreUmbrellaExposesLightweightHelpers)
{
    EXPECT_EQ(ygg::bit::ceil_div(5u, 2u), 3u);
    EXPECT_EQ(ygg::to_ms(std::chrono::seconds(1)), 1000);
    EXPECT_EQ(ygg::to_uint_t(7), ygg::uint_t { 7 });
    EXPECT_THROW(ygg::to_uint_t(static_cast<size_t>(std::numeric_limits<ygg::uint_t>::max()) + 1), std::overflow_error);

    int value = 1;
    auto observer = ygg::make_observer(value);
    EXPECT_EQ(*observer, 1);

    static_assert(Identifiable<CoreConceptFixture>);
    static_assert(InputRangeOf<std::vector<int>, int>);
    static_assert(TriviallyCopyable<int>);

    using Tuple = TypeListToTupleT<TypeList<int, double>>;
    static_assert(std::is_same_v<Tuple, std::tuple<int, double>>);
}

}
