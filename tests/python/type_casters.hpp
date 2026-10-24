/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_TESTS_PYTHON_TYPE_CASTERS_HPP_
#define YGG_TESTS_PYTHON_TYPE_CASTERS_HPP_

#include <nanobind/nanobind.h>

/// Defined in type_casters.cpp, called by type_casters_module.cpp's module macro.
void bind_type_casters_test(nanobind::module_& m);

#endif
