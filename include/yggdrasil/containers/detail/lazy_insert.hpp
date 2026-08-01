/*
 * Copyright (C) 2026 Dominik Drexler
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

#ifndef YGG_CONTAINERS_DETAIL_LAZY_INSERT_HPP_
#define YGG_CONTAINERS_DETAIL_LAZY_INSERT_HPP_

#include <cstddef>
#include <exception>
#include <functional>
#include <type_traits>
#include <utility>

namespace ygg::detail
{

template<typename Set, typename Key, typename Factory>
auto lazy_insert_with_hash(Set& set, const Key& key, size_t hash, Factory&& factory)
{
    using value_type = typename Set::value_type;
    static_assert(std::is_nothrow_default_constructible_v<value_type>);
    static_assert(std::is_nothrow_move_constructible_v<value_type>);

    bool inserted = false;
    std::exception_ptr error;
    auto it = set.lazy_emplace_with_hash(key,
                                         hash,
                                         [&](const auto& constructor)
                                         {
                                             inserted = true;
                                             try
                                             {
                                                 constructor(std::invoke(std::forward<Factory>(factory)));
                                             }
                                             catch (...)
                                             {
                                                 // GTL commits the slot before invoking this callback,
                                                 // so complete it before erasing and rethrowing below.
                                                 error = std::current_exception();
                                                 constructor(value_type {});
                                             }
                                         });

    if (error)
    {
        set.erase(it);
        std::rethrow_exception(error);
    }

    return std::pair(it, inserted);
}

template<typename Set, typename Key, typename Factory>
std::pair<typename Set::value_type, bool> lazy_insert_value_with_hash(Set& set, const Key& key, size_t hash, Factory&& factory)
{
    const auto [it, inserted] = lazy_insert_with_hash(set, key, hash, std::forward<Factory>(factory));
    return { *it, inserted };
}

}  // namespace ygg::detail

#endif
