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

#include <cmath>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <yggdrasil/serialization/json_loader.hpp>

namespace ygg::tests
{

TEST(YggdrasilTests, CommonJsonLoaderResolvePath)
{
    const auto prefix = std::filesystem::path("/tmp/prefix");

    EXPECT_EQ(common::resolve_path(prefix, "child/file.json"), prefix / "child/file.json");
    EXPECT_EQ(common::resolve_path(prefix, "/absolute/file.json"), std::filesystem::path("/absolute/file.json"));
}

TEST(YggdrasilTests, CommonJsonLoaderReadAndLoadFile)
{
    const auto path = std::filesystem::temp_directory_path() / "yggdrasil_json_loader_test.json";
    {
        auto out = std::ofstream(path);
        out << R"({"name":"case","count":3,"score":"NaN","items":[1,2],"nested":{"enabled":true},"ratio":1.5,"unsigned_score":7})";
    }

    const auto value = common::load_json_file(path);
    const auto& object = common::as_object(value, "root");

    EXPECT_EQ(common::as_string(object, "name", "root"), "case");
    EXPECT_EQ(common::as_size(object, "count", "root"), 3);
    EXPECT_TRUE(std::isnan(common::as_double(object, "score", "root")));
    EXPECT_EQ(common::as_array(object, "items", "root").size(), 2);
    EXPECT_TRUE(common::as_bool(common::as_object(object, "nested", "root"), "enabled", "root.nested"));
    EXPECT_EQ(common::find_string(object, "name", "root"), std::optional<std::string>("case"));
    EXPECT_EQ(common::find_size(object, "count", "root"), std::optional<size_t>(3));
    EXPECT_EQ(common::find_uint_t(object, "count", "root"), std::optional<uint_t>(3));
    EXPECT_EQ(common::find_bool(common::as_object(object, "nested", "root"), "enabled", "root.nested"), std::optional<bool>(true));
    EXPECT_EQ(common::find_double(object, "ratio", "root"), std::optional<double>(1.5));
    EXPECT_EQ(common::as_double(object, "unsigned_score", "root"), 7.0);

    const auto nested = common::find_object(object, "nested", "root");
    ASSERT_TRUE(nested.has_value());
    EXPECT_TRUE(common::as_bool(nested->get(), "enabled", "root.nested"));
    const auto items = common::find_array(object, "items", "root");
    ASSERT_TRUE(items.has_value());
    EXPECT_EQ(items->get().size(), 2);

    EXPECT_EQ(common::find_string(object, "missing", "root"), std::nullopt);
    EXPECT_EQ(common::find_bool(object, "missing", "root"), std::nullopt);
    EXPECT_EQ(common::find_object(object, "missing", "root"), std::nullopt);
    EXPECT_EQ(common::find_array(object, "missing", "root"), std::nullopt);

    std::filesystem::remove(path);
}

TEST(YggdrasilTests, CommonJsonLoaderReportsParseErrorsWithPath)
{
    const auto path = std::filesystem::temp_directory_path() / "yggdrasil_json_loader_invalid_test.json";
    {
        auto out = std::ofstream(path);
        out << R"({"name":)";
    }

    try
    {
        static_cast<void>(common::load_json_file(path));
        FAIL() << "Expected invalid JSON to throw.";
    }
    catch (const std::runtime_error& e)
    {
        const auto message = std::string(e.what());
        EXPECT_NE(message.find("Could not parse JSON file:"), std::string::npos);
        EXPECT_NE(message.find(path.string()), std::string::npos);
    }

    std::filesystem::remove(path);
}

TEST(YggdrasilTests, CommonJsonLoaderAccessesOptionalAndRequiredMembers)
{
    const auto value = boost::json::parse(R"({"name":"case"})");
    const auto& object = common::as_object(value, "root");

    ASSERT_NE(common::find_member(object, "name"), nullptr);
    EXPECT_EQ(common::find_member(object, "missing"), nullptr);
    EXPECT_EQ(common::as_string(common::require_member(object, "name", "root"), "root.name"), "case");
    EXPECT_THROW(common::require_member(object, "missing", "root"), std::runtime_error);
}

TEST(YggdrasilTests, CommonJsonLoaderReportsTypedErrors)
{
    const auto value = boost::json::parse(R"({"name":3,"count":-1,"enabled":1,"score":{},"items":1,"nested":[]})");
    const auto& object = common::as_object(value, "root");

    EXPECT_THROW(common::as_string(object, "name", "root"), std::runtime_error);
    EXPECT_THROW(common::as_size(object, "count", "root"), std::runtime_error);
    EXPECT_THROW(common::as_bool(object, "enabled", "root"), std::runtime_error);
    EXPECT_THROW(common::as_double(object, "score", "root"), std::runtime_error);
    EXPECT_THROW(common::as_array(object, "items", "root"), std::runtime_error);
    EXPECT_THROW(common::as_object(object, "nested", "root"), std::runtime_error);
    EXPECT_THROW(common::as_array(value, "root"), std::runtime_error);
    EXPECT_THROW(common::find_string(object, "name", "root"), std::runtime_error);
    EXPECT_THROW(common::find_size(object, "count", "root"), std::runtime_error);
    EXPECT_THROW(common::find_uint_t(object, "count", "root"), std::runtime_error);
    EXPECT_THROW(common::find_bool(object, "enabled", "root"), std::runtime_error);
    EXPECT_THROW(common::find_double(object, "score", "root"), std::runtime_error);
    EXPECT_THROW(common::find_array(object, "items", "root"), std::runtime_error);
    EXPECT_THROW(common::find_object(object, "nested", "root"), std::runtime_error);
}

}  // namespace ygg::tests
