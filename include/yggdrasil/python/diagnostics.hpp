/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_PYTHON_DIAGNOSTICS_HPP_
#define YGG_PYTHON_DIAGNOSTICS_HPP_

#include "yggdrasil/diagnostics/diagnostic.hpp"
#include "yggdrasil/diagnostics/warnings.hpp"

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>

namespace ygg
{

/// Preserve each library's exception hierarchy and attach an owned diagnostic.
/// Error must provide what() and diagnostic() returning const diagnostics::Diagnostic&.
template<typename Error>
nanobind::exception<Error> bind_diagnostic_exception(nanobind::handle scope, const char* name, nanobind::handle base = PyExc_Exception)
{
    namespace nb = nanobind;
    nb::module_::import_("pyyggdrasil.diagnostics");
    auto type = nb::exception<Error>(scope, name, base);
    if (!nb::hasattr(base, "diagnostic"))
        type.attr("diagnostic") = nb::module_::import_("builtins")
                                      .attr("property")(nb::cpp_function(
                                          [](nb::handle self)
                                          {
                                              if (nb::hasattr(self, "_diagnostic"))
                                                  return nb::borrow<nb::object>(self.attr("_diagnostic"));
                                              return nb::cast(diagnostics::Diagnostic { nb::cast<std::string>(nb::str(self)) });
                                          },
                                          nb::is_method(),
                                          nb::sig("def diagnostic(self, /) -> pyyggdrasil.diagnostics.Diagnostic")));

    nb::register_exception_translator(
        [](const std::exception_ptr& pointer, void* payload)
        {
            try
            {
                std::rethrow_exception(pointer);
            }
            catch (const Error& error)
            {
                auto error_type = nb::borrow<nb::object>(static_cast<PyObject*>(payload));
                auto instance = error_type(error.what());
                instance.attr("_diagnostic") = nb::cast(error.diagnostic(), nb::rv_policy::copy);
                PyErr_SetObject(error_type.ptr(), instance.ptr());
            }
        },
        type.ptr());
    return type;
}

/// Issues each collected warning as a Python warning of category (UserWarning by default), so it can be filtered,
/// silenced or escalated with Python's warnings module; raises if a filter turns a warning into an error.
inline void issue_warnings(const diagnostics::Warnings& warnings, nanobind::handle category = PyExc_UserWarning)
{
    for (const auto& warning : warnings.get())
        if (PyErr_WarnEx(category.ptr(), diagnostics::format_diagnostic(warning).c_str(), 1) != 0)
            throw nanobind::python_error();
}

}  // namespace ygg

#endif
