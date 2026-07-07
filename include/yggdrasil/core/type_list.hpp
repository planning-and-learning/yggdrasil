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

#ifndef YGG_CORE_TYPE_LIST_HPP_
#define YGG_CORE_TYPE_LIST_HPP_

#include <tuple>

namespace ygg
{

template<typename... Ts>
struct TypeList
{
};

template<template<typename...> typename T, typename List>
struct ApplyTypeList;

template<template<typename...> typename T, typename... Ts>
struct ApplyTypeList<T, TypeList<Ts...>>
{
    using type = T<Ts...>;
};

template<template<typename...> typename T, typename List>
using ApplyTypeListT = typename ApplyTypeList<T, List>::type;

template<template<typename> typename T, typename List>
struct MapTypeList;

template<template<typename> typename T, typename... Ts>
struct MapTypeList<T, TypeList<Ts...>>
{
    using type = TypeList<T<Ts>...>;
};

template<template<typename> typename T, typename List>
using MapTypeListT = typename MapTypeList<T, List>::type;

template<template<typename, typename> typename T, typename Bound, typename List>
struct MapTypeListSecond;

template<template<typename, typename> typename T, typename Bound, typename... Ts>
struct MapTypeListSecond<T, Bound, TypeList<Ts...>>
{
    using type = TypeList<T<Bound, Ts>...>;
};

template<template<typename, typename> typename T, typename Bound, typename List>
using MapTypeListSecondT = typename MapTypeListSecond<T, Bound, List>::type;

template<typename... Lists>
struct ConcatTypeLists;

template<>
struct ConcatTypeLists<>
{
    using type = TypeList<>;
};

template<typename... Ts>
struct ConcatTypeLists<TypeList<Ts...>>
{
    using type = TypeList<Ts...>;
};

template<typename... Lhs, typename... Rhs, typename... Rest>
struct ConcatTypeLists<TypeList<Lhs...>, TypeList<Rhs...>, Rest...>
{
    using type = typename ConcatTypeLists<TypeList<Lhs..., Rhs...>, Rest...>::type;
};

template<typename... Lists>
using ConcatTypeListsT = typename ConcatTypeLists<Lists...>::type;

template<typename List>
using TypeListToTupleT = ApplyTypeListT<std::tuple, List>;

}

#endif
