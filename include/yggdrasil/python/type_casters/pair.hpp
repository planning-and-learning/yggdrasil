/*
    nanobind/stl/pair.h: type caster for std::pair<...>

    Copyright (c) 2022 Wenzel Jakob

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE file.
*/

#pragma once

#include <nanobind/nanobind.h>
#include <type_traits>
#include <utility>
#include <yggdrasil/containers/pair.hpp>

NAMESPACE_BEGIN(NB_NAMESPACE)
NAMESPACE_BEGIN(detail)

// Adapted from nanobind/stl/pair.h
template<typename C, typename T1, typename T2>
struct type_caster<::ygg::View<::cista::pair<T1, T2>, C>>
{
    using ViewT = ::ygg::View<::cista::pair<T1, T2>, C>;
    using First = std::conditional_t<::ygg::ViewConcept<T1, C>, ::ygg::View<T1, C>, T1>;
    using Second = std::conditional_t<::ygg::ViewConcept<T2, C>, ::ygg::View<T2, C>, T2>;
    using Caster1 = make_caster<First>;
    using Caster2 = make_caster<Second>;

    NB_TYPE_CASTER(ViewT, const_name("tuple[") + concat(Caster1::Name, Caster2::Name) + const_name("]"))

    // Views are not constructible from Python.
    bool from_python(handle, uint32_t, cleanup_list*) noexcept { return false; }

    template<typename T>
    static handle from_cpp(T&& value, rv_policy policy, cleanup_list* cleanup) noexcept
    {
        decltype(auto) first = value.get_first();
        object o1 = steal(Caster1::from_cpp(std::forward<decltype(first)>(first), policy, cleanup));
        if (!o1.is_valid())
            return {};

        decltype(auto) second = value.get_second();
        object o2 = steal(Caster2::from_cpp(std::forward<decltype(second)>(second), policy, cleanup));
        if (!o2.is_valid())
            return {};

        PyObject* result = PyTuple_New(2);
        NB_TUPLE_SET_ITEM(result, 0, o1.release().ptr());
        NB_TUPLE_SET_ITEM(result, 1, o2.release().ptr());
        return result;
    }
};

NAMESPACE_END(detail)
NAMESPACE_END(NB_NAMESPACE)
