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

#ifndef YGG_CORE_OBSERVER_PTR_EQUAL_TO_HPP_
#define YGG_CORE_OBSERVER_PTR_EQUAL_TO_HPP_

#include "yggdrasil/core/observer_ptr.hpp"
#include "yggdrasil/semantics/equal_to.hpp"

#include <type_traits>

namespace ygg
{

template<typename T>
struct EqualTo<ObserverPtr<T>>
{
    bool operator()(ObserverPtr<T> lhs, ObserverPtr<T> rhs) const noexcept { return EqualTo<std::remove_cvref_t<T>> {}(*lhs, *rhs); }
};

}  // namespace ygg

#endif
