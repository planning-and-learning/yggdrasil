#ifndef PYYGGDRASIL_SERIALIZATION_MODULE_HPP_
#define PYYGGDRASIL_SERIALIZATION_MODULE_HPP_

#include <nanobind/nanobind.h>

namespace yggdrasil
{
void bind_serialization_module_definitions(nanobind::module_& module);
}

#endif
