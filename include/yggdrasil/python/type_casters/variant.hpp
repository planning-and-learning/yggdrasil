/*
    nanobind/stl/variant.h: type caster for ::cista::offset::variant<...>

    Copyright (c) 2022 Yoshiki Matsuda and Wenzel Jakob

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE file.
*/

#pragma once

#include "yggdrasil/containers/variant.hpp"

#include <nanobind/nanobind.h>
#include <nanobind/stl/variant.h>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

NAMESPACE_BEGIN(NB_NAMESPACE)
NAMESPACE_BEGIN(detail)

/// ygg::ViewVariant converts like the std::variant it derives from.
template<typename Variant, typename C>
struct type_caster<::ygg::ViewVariant<Variant, C>>
{
    using Value = ::ygg::ViewVariant<Variant, C>;
    using Base = typename Value::base_type;
    using BaseCaster = make_caster<Base>;

    static constexpr auto Name = BaseCaster::Name;
    template<typename T>
    using Cast = movable_cast_t<T>;
    template<typename T>
    static constexpr bool can_cast()
    {
        return true;
    }

    // Views are not default-constructible, so the value is stored only once converted.
    std::optional<Value> value;
    explicit operator Value*() { return &*value; }
    explicit operator Value&() { return *value; }
    explicit operator Value&&() { return std::move(*value); }

    bool from_python(handle src, uint32_t flags, cleanup_list* cleanup) noexcept
    {
        BaseCaster caster;
        if (!caster.from_python(src, flags, cleanup) || !caster.template can_cast<Base>())
            return false;
        value.emplace(caster.operator cast_t<Base>());
        return true;
    }

    static handle from_cpp(const Value* src, rv_policy policy, cleanup_list* cleanup) noexcept
    {
        return src ? from_cpp(*src, policy, cleanup) : none().release();
    }
    static handle from_cpp(const Value& src, rv_policy policy, cleanup_list* cleanup) noexcept
    {
        return BaseCaster::from_cpp(static_cast<const Base&>(src), policy, cleanup);
    }
};

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
