/*
    nanobind/stl/array.h: type caster for std::array<...>

    Copyright (c) 2022 Wenzel Jakob

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE file.
*/

#pragma once

#include "yggdrasil/python/type_casters/vector.hpp"

#include <type_traits>
#include <utility>
#include <yggdrasil/containers/array.hpp>

NAMESPACE_BEGIN(NB_NAMESPACE)
NAMESPACE_BEGIN(detail)

// Adapted from nanobind/stl/detail/nb_array.h
template<typename Type, size_t Size, typename C>
struct type_caster<::ygg::View<::cista::array<Type, Size>, C>>
{
    using ViewT = ::ygg::View<::cista::array<Type, Size>, C>;
    using Entry = std::conditional_t<::ygg::ViewConcept<Type, C>, ::ygg::View<Type, C>, Type>;
    using Caster = make_caster<Entry>;

    NB_TYPE_CASTER(ViewT, io_name("collections.abc.Sequence", "list") + const_name("[") + Caster::Name + const_name("]"))

    // Views are not constructible from Python.
    bool from_python(handle, uint8_t, cleanup_list*) noexcept { return false; }

    template<typename T>
    static handle from_cpp(T&& src, rv_policy policy, cleanup_list* cleanup)
    {
        return sequence_view_from_cpp<Caster>(std::forward<T>(src), policy, cleanup);
    }
};

NAMESPACE_END(detail)
NAMESPACE_END(NB_NAMESPACE)
