/*
 * Copyright (C) 2023 Dominik Drexler and Simon Stahlberg
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

#ifndef YGG_COMMON_ITERTOOLS_HPP_
#define YGG_COMMON_ITERTOOLS_HPP_

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <vector>

namespace ygg::itertools
{

namespace cartesian_set
{

template<typename T>
struct Workspace
{
    std::vector<T> element;
    std::vector<std::size_t> indices;
};

template<class OuterRandomIt, class InnerRange = typename std::iterator_traits<OuterRandomIt>::value_type, class T = typename InnerRange::value_type, class F>
void for_each_element(OuterRandomIt first, OuterRandomIt last, Workspace<T>& workspace, F&& callback)
{
    const std::size_t n = last - first;

    workspace.element.resize(n);
    workspace.indices.assign(n, 0);

    if (n == 0)
    {
        callback(workspace.element);  // empty element for empty range
        return;
    }

    if (std::any_of(first, last, [](auto&& inner) { return std::distance(inner.begin(), inner.end()) == 0; }))
        return;

    while (true)
    {
        // Emit current tuple
        for (std::size_t i = 0; i < n; ++i)
            workspace.element[i] = first[i][workspace.indices[i]];

        callback(workspace.element);

        // Mixed-radix increment (odometer)
        std::size_t pos = n - 1;
        while (true)
        {
            ++workspace.indices[pos];
            if (workspace.indices[pos] < first[pos].size())
                break;

            workspace.indices[pos] = 0;
            if (pos == 0)
                return;  // fully done

            --pos;
        }
    }
}

}  // namespace cartesian_set

}  // namespace ygg::itertools

#endif
