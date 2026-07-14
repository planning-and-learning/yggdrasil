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

#ifndef YGG_CONTAINERS_PAIR_HPP_
#define YGG_CONTAINERS_PAIR_HPP_

#include "yggdrasil/core/types_utils.hpp"

#include <cista/containers/pair.h>

namespace ygg
{

template<typename T1, typename T2, typename C>
class View<::cista::pair<T1, T2>, C>
{
public:
    using Pair = ::cista::pair<T1, T2>;

    View(const Pair& handle, const C& context) noexcept : m_context(&context), m_handle(&handle) {}

    decltype(auto) get_first() const noexcept
    {
        if constexpr (ViewConcept<T1, C>)
            return make_view(get_data().first, get_context());
        else
            return (get_data().first);
    }

    decltype(auto) get_second() const noexcept
    {
        if constexpr (ViewConcept<T2, C>)
            return make_view(get_data().second, get_context());
        else
            return (get_data().second);
    }

    const auto& get_data() const noexcept { return *m_handle; }
    const auto& get_context() const noexcept { return *m_context; }
    const auto& get_handle() const noexcept { return *m_handle; }

private:
    const C* m_context;
    const Pair* m_handle;
};

}  // namespace ygg

#endif
