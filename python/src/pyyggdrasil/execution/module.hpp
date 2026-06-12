#ifndef PYYGGDRASIL_EXECUTION_MODULE_HPP_
#define PYYGGDRASIL_EXECUTION_MODULE_HPP_

#include <nanobind/nanobind.h>

namespace yggdrasil
{
namespace nb = nanobind;

void bind_execution_module_definitions(nb::module_& m);

}  // namespace yggdrasil

#endif
