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

#pragma once

#include "yggdrasil/core/closed_interval.hpp"

#include <concepts>
#include <nanobind/nanobind.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/variant.h>
#include <optional>
#include <tuple>
#include <type_traits>
#include <variant>

NAMESPACE_BEGIN(NB_NAMESPACE)
NAMESPACE_BEGIN(detail)

template<std::floating_point T>
struct type_caster<::ygg::ClosedInterval<T>>
{
    using Interval = ::ygg::ClosedInterval<T>;
    using Bounds = std::tuple<T, T>;
    using PythonValue = std::optional<std::variant<T, Bounds>>;
    using PythonCaster = make_caster<PythonValue>;
    using ScalarCaster = make_caster<T>;
    using BoundsCaster = make_caster<Bounds>;

    NB_TYPE_CASTER(Interval, PythonCaster::Name)

    bool from_python(handle src, uint8_t flags, cleanup_list* cleanup) noexcept
    {
        auto caster = PythonCaster {};
        if (!caster.from_python(src, flags_for_local_caster<PythonValue>(flags), cleanup) || !caster.template can_cast<PythonValue>())
            return false;

        auto input = caster.operator cast_t<PythonValue>();
        if (!input)
        {
            value = Interval {};
        }
        else if (const auto* singleton = std::get_if<T>(&*input))
        {
            value = Interval(*singleton, *singleton);
        }
        else
        {
            const auto& [lower, upper] = std::get<Bounds>(*input);
            value = Interval(lower, upper);
        }
        return true;
    }

    static handle from_cpp(const Interval& value, rv_policy policy, cleanup_list* cleanup) noexcept
    {
        if (empty(value))
            return none().release();

        const auto lower_bound = lower(value);
        const auto upper_bound = upper(value);
        if (lower_bound == upper_bound)
            return ScalarCaster::from_cpp(lower_bound, policy, cleanup);

        return BoundsCaster::from_cpp(Bounds { lower_bound, upper_bound }, policy, cleanup);
    }
};

template<std::floating_point T>
struct has_arg_defaults<::ygg::ClosedInterval<T>> : std::true_type
{
};

NAMESPACE_END(detail)
NAMESPACE_END(NB_NAMESPACE)
