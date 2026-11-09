#ifndef PYYGGDRASIL_FORMALISM_MODULE_HPP_
#define PYYGGDRASIL_FORMALISM_MODULE_HPP_

#include <nanobind/nanobind.h>

namespace yggdrasil
{
namespace nb = nanobind;

/// The free relations: column types and layouts, rows, relation builders and views, and their repository.
void bind_formalism_module_definitions(nb::module_& m);

}  // namespace yggdrasil

#endif
