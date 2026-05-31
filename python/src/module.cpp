#include "pyyggdrasil/module.hpp"

#include <nanobind/nanobind.h>

namespace nb = nanobind;

NB_MODULE(_pyyggdrasil, m)
{
    yggdrasil::bind_module_definitions(m);
}
