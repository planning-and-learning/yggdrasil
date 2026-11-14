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

#include "yggdrasil/formalism/intern_instantiations.hpp"

#include <boost/mp11.hpp>
#include <gtest/gtest.h>
#include <type_traits>

namespace
{

using Types = boost::mp11::mp_list<int, double, char>;

TEST(InternInstantiations, TheIthTypeOfTheList)
{
    static_assert(std::is_same_v<ygg::formalism::detail::InternType<Types, 0>, int>);
    static_assert(std::is_same_v<ygg::formalism::detail::InternType<Types, 1>, double>);
    static_assert(std::is_same_v<ygg::formalism::detail::InternType<Types, 2>, char>);
    SUCCEED();
}

}  // namespace
