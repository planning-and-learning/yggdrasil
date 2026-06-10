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
#include <string>

namespace ygg::tests {

TEST(YggdrasilTests, CommonPathResolvePath) {
  const auto prefix = std::filesystem::path("/tmp/prefix");

  EXPECT_EQ(common::resolve_path(prefix, "child/file.txt"),
            prefix / "child/file.txt");
  EXPECT_EQ(common::resolve_path(prefix, "/absolute/file.txt"),
            std::filesystem::path("/absolute/file.txt"));
}

TEST(YggdrasilTests, CommonPathReadFile) {
  const auto path =
      std::filesystem::temp_directory_path() / "yggdrasil_path_test.txt";
  {
    auto out = std::ofstream(path);
    out << "contents";
  }

  EXPECT_EQ(common::read_file(path), "contents");
  std::filesystem::remove(path);
}

TEST(YggdrasilTests, CommonPathReadFilePreservesBinaryContent) {
  const auto path =
      std::filesystem::temp_directory_path() / "yggdrasil_path_binary_test.bin";
  const auto contents = std::string("a\0b\n", 4);
  {
    auto out = std::ofstream(path, std::ios::binary);
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  }

  EXPECT_EQ(common::read_file(path), contents);
  std::filesystem::remove(path);
}

TEST(YggdrasilTests, CommonPathReadFileReportsMissingFile) {
  const auto path =
      std::filesystem::temp_directory_path() / "yggdrasil_path_missing.txt";
  std::filesystem::remove(path);

  try {
    (void)common::read_file(path);
    FAIL() << "Expected read_file to reject missing files";
  } catch (const std::runtime_error &error) {
    const auto message = std::string(error.what());
    EXPECT_NE(message.find("Could not open file:"), std::string::npos);
    EXPECT_NE(message.find(path.string()), std::string::npos);
  }
}

} // namespace ygg::tests
