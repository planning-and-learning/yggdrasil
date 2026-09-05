#ifndef PYYGGDRASIL_DIAGNOSTICS_MODULE_HPP_
#define PYYGGDRASIL_DIAGNOSTICS_MODULE_HPP_

#include <nanobind/nanobind.h>

namespace yggdrasil
{
namespace nb = nanobind;

void bind_diagnostics_module_definitions(nb::module_& m);

}  // namespace yggdrasil

#endif
