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
#include <yggdrasil/serialization/json.hpp>

#include <cmath>
#include <limits>
#include <optional>
#include <string>

namespace ygg::tests
{

TEST(YggdrasilTests, CommonJsonHeaderAccessesRequiredAndOptionalMembers)
{
    const auto value = boost::json::parse(R"({"name":"case","count":3,"enabled":true,"score":"NaN","ratio":1.5,"metadata":{"kind":"fixture"},"items":[1,2]})");
    const auto& object = common::as_object(value, "root");

    EXPECT_EQ(common::as_string(object, "name", "root"), "case");
    EXPECT_EQ(common::as_size(object, "count", "root"), 3);
    EXPECT_EQ(common::as_uint_t(object, "count", "root"), ygg::uint_t { 3 });
    EXPECT_TRUE(common::as_bool(object, "enabled", "root"));
    EXPECT_TRUE(std::isnan(common::as_double(object, "score", "root")));
    EXPECT_EQ(common::as_object(object, "metadata", "root").size(), 1);
    EXPECT_EQ(common::as_array(object, "items", "root").size(), 2);
    ASSERT_TRUE(common::find_object(object, "metadata", "root"));
    EXPECT_EQ(common::find_object(object, "metadata", "root")->get().size(), 1);
    ASSERT_TRUE(common::find_array(object, "items", "root"));
    EXPECT_EQ(common::find_array(object, "items", "root")->get().size(), 2);
    EXPECT_EQ(common::find_string(object, "name", "root"), std::optional<std::string>("case"));
    EXPECT_EQ(common::find_size(object, "count", "root"), std::optional<size_t>(3));
    EXPECT_EQ(common::find_uint_t(object, "count", "root"), std::optional<ygg::uint_t>(3));
    EXPECT_EQ(common::find_bool(object, "enabled", "root"), std::optional<bool>(true));
    EXPECT_EQ(common::find_double(object, "ratio", "root"), std::optional<double>(1.5));
    EXPECT_EQ(common::find_object(object, "missing", "root"), std::nullopt);
    EXPECT_EQ(common::find_array(object, "missing", "root"), std::nullopt);
    EXPECT_EQ(common::find_string(object, "missing", "root"), std::nullopt);
    EXPECT_EQ(common::find_uint_t(object, "missing", "root"), std::nullopt);
    EXPECT_EQ(common::find_bool(object, "missing", "root"), std::nullopt);
}

TEST(YggdrasilTests, CommonJsonHeaderReportsTypedErrors)
{
    const auto value = boost::json::parse(R"({"name":3,"count":-1,"enabled":1,"score":{},"items":1})");
    const auto& object = common::as_object(value, "root");

    EXPECT_THROW(common::as_string(object, "name", "root"), std::runtime_error);
    EXPECT_THROW(common::as_size(object, "count", "root"), std::runtime_error);
    EXPECT_THROW(common::as_uint_t(object, "count", "root"), std::runtime_error);
    EXPECT_THROW(common::as_bool(object, "enabled", "root"), std::runtime_error);
    EXPECT_THROW(common::as_double(object, "score", "root"), std::runtime_error);
    EXPECT_THROW(common::find_object(object, "items", "root"), std::runtime_error);
    EXPECT_THROW(common::as_array(object, "items", "root"), std::runtime_error);
    EXPECT_THROW(common::find_array(object, "name", "root"), std::runtime_error);
    const auto too_large_for_uint = boost::json::value(static_cast<int64_t>(std::numeric_limits<ygg::uint_t>::max()) + 1);
    EXPECT_THROW(common::as_uint_t(too_large_for_uint, "root.count"), std::runtime_error);
    EXPECT_THROW(common::require_member(object, "missing", "root"), std::runtime_error);
}

}
