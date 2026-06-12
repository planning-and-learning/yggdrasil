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
#include <sstream>
#include <type_traits>
#include <yggdrasil.hpp>

namespace ygg::tests
{

TEST(YggdrasilTests, CommonUmbrellaHeaderCompiles) { SUCCEED(); }

TEST(YggdrasilTests, CommonIostreamIndentationTracksPerStreamScope)
{
    static_assert(!std::is_copy_constructible_v<ygg::IndentScope>);
    static_assert(!std::is_move_constructible_v<ygg::IndentScope>);

    auto out = std::ostringstream();
    auto other = std::ostringstream();

    ygg::print_indent(out) << "root";
    {
        const auto scope = ygg::IndentScope(out);
        ygg::print_indent(out) << "child";
        ygg::print_indent(other) << "other";
    }
    ygg::print_indent(out) << "root";

    EXPECT_EQ(out.str(), "root    childroot");
    EXPECT_EQ(other.str(), "other");
}

TEST(YggdrasilTests, CommonIostreamIndentationClampsNegativeLevels)
{
    auto out = std::ostringstream();

    out << ygg::indent_down;
    ygg::print_indent(out) << "text";
    out << ygg::indent_up;
    ygg::print_indent(out) << "child";

    EXPECT_EQ(out.str(), "text    child");
}

}  // namespace ygg::tests
