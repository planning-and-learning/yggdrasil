/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_PYTHON_OWNER_HPP_
#define YGG_PYTHON_OWNER_HPP_

#include <nanobind/nanobind.h>
#include <nanobind/stl/pair.h>
#include <utility>

namespace ygg::python
{
namespace nb = nanobind;

inline nb::object make_owner_retainer()
{
    return nb::cpp_function([](nb::object value, nb::handle) { return value; }, nb::keep_alive<0, 2>());
}

/// Retain an owner on borrowed instance leaves, including leaves nested in
/// Python containers produced by type casters. Scalars require no retention.
inline nb::object retain_owner_tree(nb::handle value, nb::handle owner, const nb::object& retainer)
{
    if (nb::isinstance<nb::tuple>(value) || nb::isinstance<nb::list>(value))
    {
        for (nb::handle child : nb::borrow<nb::object>(value))
            retain_owner_tree(child, owner, retainer);
    }
    else if (nb::isinstance<nb::dict>(value))
    {
        for (const auto& [key, child] : nb::borrow<nb::dict>(value))
        {
            retain_owner_tree(key, owner, retainer);
            retain_owner_tree(child, owner, retainer);
        }
    }
    else if (nb::inst_check(value) && !value.is(owner))
        retainer(value, owner);
    return nb::borrow<nb::object>(value);
}

template<typename T>
nb::object cast_with_owner(T&& value, nb::handle owner, const nb::object& retainer)
{
    return retain_owner_tree(nb::cast(std::forward<T>(value)), owner, retainer);
}

/// Keep the iterator owner alive and retain it on every yielded borrowed leaf.
inline nb::object make_iterator_with_owner(nb::object iterator, nb::handle owner, const nb::object& retainer)
{
    auto retain = nb::cpp_function([owner = nb::borrow<nb::object>(owner), retainer](nb::object value) { return retain_owner_tree(value, owner, retainer); });
    return nb::module_::import_("builtins").attr("map")(std::move(retain), std::move(iterator));
}

}  // namespace ygg::python

#endif
