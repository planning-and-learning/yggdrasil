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

#ifndef YGG_COMMON_TUPLE_HPP_
#define YGG_COMMON_TUPLE_HPP_

#include <cstddef>
#include <functional>
#include <tuple>
#include <utility>

namespace ygg
{

/**
 * std::get<I> with runtime I.
 */

template<size_t I>
struct visit_impl
{
    template<typename T, typename F>
    static bool visit(T&& tup, size_t idx, F&& function)
    {
        if (idx == I - 1)
        {
            std::invoke(std::forward<F>(function), std::get<I - 1>(std::forward<T>(tup)));
            return true;
        }
        return visit_impl<I - 1>::visit(std::forward<T>(tup), idx, std::forward<F>(function));
    }
};

template<>
struct visit_impl<0>
{
    template<typename T, typename F>
    static bool visit(T&&, size_t, F&&) noexcept
    {
        return false;
    }
};

template<typename F, typename... Ts>
bool visit_at(std::tuple<Ts...> const& tup, size_t idx, F&& function)
{
    return visit_impl<sizeof...(Ts)>::visit(tup, idx, std::forward<F>(function));
}

template<typename F, typename... Ts>
bool visit_at(std::tuple<Ts...>& tup, size_t idx, F&& function)
{
    return visit_impl<sizeof...(Ts)>::visit(tup, idx, std::forward<F>(function));
}

}  // namespace ygg

#endif
