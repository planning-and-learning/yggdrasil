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
#include <yggdrasil/io/project_path.hpp>

#include <filesystem>

namespace ygg::tests
{

TEST(YggdrasilTests, CommonProjectPathRootDerivedPaths)
{
#ifdef ROOT_DIR
    const auto root = std::filesystem::path(std::string(ROOT_DIR));

    EXPECT_EQ(common::root_path(), root);
    EXPECT_EQ(common::data_path("domain/file.pddl"), root / "data" / "domain/file.pddl");
    EXPECT_EQ(common::profiling_path("case.json"), root / "profiling" / "case.json");
#else
    GTEST_SKIP() << "ROOT_DIR is not configured.";
#endif
}

}
