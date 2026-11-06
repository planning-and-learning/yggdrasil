#include "pyyggdrasil/module.hpp"
#include "yggdrasil/python/type_casters.hpp"

#include <nanobind/nanobind.h>

namespace nb = nanobind;

NB_MODULE(_pyyggdrasil, m) { yggdrasil::bind_module_definitions(m); }
