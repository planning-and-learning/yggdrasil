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
#include <yggdrasil/core/path.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace ygg::tests
{

TEST(YggdrasilTests, CommonPathResolvePath)
{
    const auto prefix = std::filesystem::path("/tmp/prefix");

    EXPECT_EQ(common::resolve_path(prefix, "child/file.txt"), prefix / "child/file.txt");
    EXPECT_EQ(common::resolve_path(prefix, "/absolute/file.txt"), std::filesystem::path("/absolute/file.txt"));
}

TEST(YggdrasilTests, CommonPathReadFile)
{
    const auto path = std::filesystem::temp_directory_path() / "yggdrasil_path_test.txt";
    {
        auto out = std::ofstream(path);
        out << "contents";
    }

    EXPECT_EQ(common::read_file(path), "contents");
    std::filesystem::remove(path);
}

TEST(YggdrasilTests, CommonPathReadFileReportsMissingFile)
{
    const auto path = std::filesystem::temp_directory_path() / "yggdrasil_path_missing.txt";
    std::filesystem::remove(path);

    EXPECT_THROW(common::read_file(path), std::runtime_error);
}

}
