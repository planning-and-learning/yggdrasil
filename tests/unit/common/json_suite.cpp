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
#include <yggdrasil/serialization/json_suite.hpp>

#include <boost/json.hpp>
#include <filesystem>
#include <stdexcept>

namespace ygg::tests {

TEST(YggdrasilTests, CommonJsonSuitePrefixDefaultsToRoot) {
#ifdef ROOT_DIR
  const auto suite = boost::json::object();

  EXPECT_EQ(common::suite_prefix_path(suite), common::root_path());
  EXPECT_EQ(common::suite_path(suite, "tests/file.json"),
            common::root_path() / "tests/file.json");
#else
  GTEST_SKIP() << "ROOT_DIR is not configured.";
#endif
}

TEST(YggdrasilTests, CommonJsonSuitePrefixResolvesRelativePaths) {
#ifdef ROOT_DIR
  auto suite = boost::json::object();
  suite["prefix"] = "data";

  EXPECT_EQ(common::suite_prefix_path(suite), common::root_path() / "data");
  EXPECT_EQ(common::suite_path(suite, "task.pddl"),
            common::root_path() / "data" / "task.pddl");
#else
  GTEST_SKIP() << "ROOT_DIR is not configured.";
#endif
}

TEST(YggdrasilTests, CommonJsonSuiteMemberPathReadsRequiredStringMembers) {
#ifdef ROOT_DIR
  auto suite = boost::json::object();
  suite["prefix"] = "data";
  suite["domain"] = "domain.pddl";

  EXPECT_EQ(common::suite_member_path(suite, "domain"),
            common::root_path() / "data" / "domain.pddl");

  EXPECT_THROW(common::suite_member_path(suite, "problem"), std::runtime_error);
  suite["problem"] = 42;
  EXPECT_THROW(common::suite_member_path(suite, "problem"), std::runtime_error);
#else
  GTEST_SKIP() << "ROOT_DIR is not configured.";
#endif
}

TEST(YggdrasilTests, CommonJsonSuitePrefixKeepsAbsolutePaths) {
#ifdef ROOT_DIR
  auto suite = boost::json::object();
  suite["prefix"] = "/absolute/prefix";

  EXPECT_EQ(common::suite_prefix_path(suite),
            std::filesystem::path("/absolute/prefix"));
  EXPECT_EQ(common::suite_path(suite, "/absolute/task.pddl"),
            std::filesystem::path("/absolute/task.pddl"));
#else
  GTEST_SKIP() << "ROOT_DIR is not configured.";
#endif
}

TEST(YggdrasilTests, CommonJsonSuitePrefixReportsTypeErrors) {
#ifdef ROOT_DIR
  auto suite = boost::json::object();
  suite["prefix"] = 42;

  EXPECT_THROW(common::suite_prefix_path(suite), std::runtime_error);
#else
  GTEST_SKIP() << "ROOT_DIR is not configured.";
#endif
}

} // namespace ygg::tests
