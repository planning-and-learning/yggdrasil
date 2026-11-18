/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_PYTHON_RELATIONS_HPP_
#define YGG_PYTHON_RELATIONS_HPP_

// What the Python bindings of relations share (pyyggdrasil.formalism's relation types, pytyr.database's operations).

#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/row_view.hpp"
#include "yggdrasil/formalism/values.hpp"

#include <cstdint>
#include <nanobind/nanobind.h>
#include <nanobind/stl/variant.h>
#include <variant>

namespace ygg
{
/// A Python value as a value of a column of `type`; nanobind::type_error where it is not one.
inline ColumnValue cast_value(nanobind::handle value, TypeId type)
{
    auto result = decode(0, type);  // the alternative of `type`
    std::visit(
        [value](auto& alternative)
        {
            if (!nanobind::try_cast(value, alternative))
                throw nanobind::type_error("Value does not match the column type.");
        },
        result);
    return result;
}
}  // namespace ygg

#endif
