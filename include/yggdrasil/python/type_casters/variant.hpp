/*
    nanobind/stl/variant.h: type caster for ::cista::offset::variant<...>

    Copyright (c) 2022 Yoshiki Matsuda and Wenzel Jakob

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE file.
*/

#pragma once

#include <nanobind/nanobind.h>
#include <nanobind/stl/variant.h>
#include <type_traits>
#include <utility>
#include <variant>
#include <yggdrasil/containers/variant.hpp>

NAMESPACE_BEGIN(NB_NAMESPACE)
NAMESPACE_BEGIN(detail)

// Adapted from nanobind/stl/variant.h
template<typename C, typename... Ts>
struct type_caster<::ygg::View<::cista::offset::variant<Ts...>, C>>
{
    using ViewT = ::ygg::View<::cista::offset::variant<Ts...>, C>;

    // Optional nice typing: Union[...]
    static constexpr auto Name = union_name(make_caster<std::conditional_t<::ygg::ViewConcept<Ts, C>, ::ygg::View<Ts, C>, Ts>>::Name...);

    // No Python -> C++ conversion
    bool from_python(handle, uint32_t, cleanup_list*) noexcept { return false; }

    template<typename U>
    static handle from_cpp(U&& v, rv_policy policy, cleanup_list* cleanup) noexcept
    {
        if (!v.valid())
            return none().release();

        // Use View::apply so ViewConcept alternatives are exposed as views.
        return v.apply(
            [&](auto&& arg) -> handle
            {
                using A = std::decay_t<decltype(arg)>;
                return make_caster<A>::from_cpp(std::forward<decltype(arg)>(arg), policy, cleanup);
            });
    }
};

// Adapted from nanobind/stl/variant.h
template<typename... Ts>
struct type_caster<::cista::offset::variant<Ts...>>
{
    using Variant = ::cista::offset::variant<Ts...>;

    // Cista variants are default-constructible even if their alternatives are not.
    NB_TYPE_CASTER(Variant, union_name(make_caster<Ts>::Name...))

    template<typename T>
    bool try_variant(const handle& src, uint32_t flags, cleanup_list* cleanup)
    {
        using CasterT = make_caster<T>;

        CasterT caster;

        if (!caster.from_python(src, flags_for_local_caster<T>(flags), cleanup) || !caster.template can_cast<T>())
            return false;

        value = caster.operator cast_t<T>();

        return true;
    }

    bool from_python(handle src, uint32_t flags, cleanup_list* cleanup) noexcept
    {
        if (flags & (uint32_t) cast_flags::convert)
        {
            if ((try_variant<Ts>(src, flags & ~(uint32_t) cast_flags::convert, cleanup) || ...))
            {
                return true;
            }
        }
        return (try_variant<Ts>(src, flags, cleanup) || ...);
    }

    template<typename T>
    static handle from_cpp(T&& value, rv_policy policy, cleanup_list* cleanup) noexcept
    {
        if (!value.valid())
            return none().release();

        return std::visit([&](auto&& v) { return make_caster<decltype(v)>::from_cpp(std::forward<decltype(v)>(v), policy, cleanup); }, std::forward<T>(value));
    }
};

NAMESPACE_END(detail)
NAMESPACE_END(NB_NAMESPACE)
