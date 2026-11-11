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

#ifndef YGG_PYTHON_BINDINGS_HPP_
#define YGG_PYTHON_BINDINGS_HPP_

#include "yggdrasil/core/unsigned_integer.hpp"
#include "yggdrasil/formalism/interning.hpp"
#include "yggdrasil/formatting/formatter.hpp"

#include <nanobind/nanobind.h>
#include <urd/python/bindings.hpp>
#include <urd/python/owner.hpp>

namespace ygg
{
namespace nb = nanobind;

template<typename V>
nb::class_<V>& add_print(nb::class_<V>& cls)
{
    return cls  //
        .def("__str__", [](const V& self) { return to_string(self); })
        .def("__repr__", [](const V& self) { return to_string(self); });
}

/// Binds a nonnegative integer of its own type (UnsignedInteger): built from an int (default 0), read by int(),
/// index() and value, compared and hashed as its value.
template<typename T>
void bind_unsigned_integer(nb::module_& m, const std::string& name)
{
    auto cls = nb::class_<T>(m, name.c_str());
    cls.def(nb::init<>())
        .def(nb::init<typename T::value_type>(), nb::arg("value"))
        .def("__int__", [](const T& i) { return i.value; })
        .def("__index__", [](const T& i) { return i.value; })
        .def_ro("value", &T::value);
    urd::python::add_comparison(cls);
    cls.def("__hash__", [](const T& i) { return i.value; }).def("__repr__", [name](const T& i) { return name + "(" + std::to_string(i.value) + ")"; });
}

/// Binds repository.insert(data) -> (view, created); the view keeps the repository alive. Takes data by reference, as
/// C++ insert does: no copy, and the caller's object is canonicalized in place.
template<typename T, typename Repository>
void bind_insert(nb::class_<Repository>& repository)
{
    repository.def("insert", [](Repository& self, urd::data<T>& data) { return formalism::insert(self, data); }, nb::arg("data"));
}

}  // namespace ygg

#endif
