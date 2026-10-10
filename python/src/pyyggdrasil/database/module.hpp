#ifndef PYYGGDRASIL_DATABASE_MODULE_HPP_
#define PYYGGDRASIL_DATABASE_MODULE_HPP_

#include "yggdrasil/database/semantics/relation.hpp"

#include <nanobind/nanobind.h>

namespace yggdrasil
{
namespace nb = nanobind;

void bind_database_module_definitions(nb::module_& m);

/// Borrowed relation views use a relation repository only as their context.
const ygg::database::RelationRepository<>& borrowed_relation_context();

}  // namespace yggdrasil

#endif
