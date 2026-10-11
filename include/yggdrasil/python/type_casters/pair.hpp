/*
    nanobind/stl/pair.h: type caster for std::pair<...>

    Copyright (c) 2022 Wenzel Jakob

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE file.
*/

#pragma once

#include "yggdrasil/containers/pair.hpp"

#include <nanobind/nanobind.h>
#include <nanobind/stl/pair.h>
#include <type_traits>
#include <utility>

NAMESPACE_BEGIN(NB_NAMESPACE)
NAMESPACE_BEGIN(detail)

// Reuse Nanobind's pair conversions, constructing a Cista pair on input.
template<typename T1, typename T2>
struct type_caster<::cista::pair<T1, T2>> : type_caster<std::pair<T1, T2>>
{
    using Value = ::cista::pair<T1, T2>;

    template<typename T>
    using Cast = Value;

    explicit operator Value() { return Value { this->caster1.operator cast_t<T1>(), this->caster2.operator cast_t<T2>() }; }
};

// Adapted from nanobind/stl/pair.h
template<typename C, typename T1, typename T2>
struct type_caster<::ygg::View<::cista::pair<T1, T2>, C>>
{
private:
    using ViewT = ::ygg::View<::cista::pair<T1, T2>, C>;
    using First = ::ygg::ViewOrValue<T1, C>;
    using Second = ::ygg::ViewOrValue<T2, C>;
    using Caster1 = make_caster<First>;
    using Caster2 = make_caster<Second>;

public:
    NB_TYPE_CASTER(ViewT, const_name("tuple[") + concat(Caster1::Name, Caster2::Name) + const_name("]"))

    // Views are not constructible from Python.
    bool from_python(handle, uint32_t, cleanup_list*) noexcept { return false; }

    template<typename T>
    static handle from_cpp(T&& value, rv_policy policy, cleanup_list* cleanup) noexcept
    {
        object o1 = steal(Caster1::from_cpp(value.get_first(), policy, cleanup));
        if (!o1.is_valid())
            return {};

        object o2 = steal(Caster2::from_cpp(value.get_second(), policy, cleanup));
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
