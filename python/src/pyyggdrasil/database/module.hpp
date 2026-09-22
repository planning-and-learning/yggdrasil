#ifndef PYYGGDRASIL_DATABASE_MODULE_HPP_
#define PYYGGDRASIL_DATABASE_MODULE_HPP_

#include <nanobind/nanobind.h>

namespace yggdrasil
{
namespace nb = nanobind;

void bind_database_module_definitions(nb::module_& m);

}  // namespace yggdrasil

#endif
