#ifndef PYYGGDRASIL_DATABASE_MODULE_HPP_
#define PYYGGDRASIL_DATABASE_MODULE_HPP_

#include "yggdrasil/database/semantics/relation.hpp"

#include <cstddef>
#include <cstdint>
#include <nanobind/nanobind.h>

namespace yggdrasil
{
namespace nb = nanobind;

/// Python's ColumnType is the closed set of storage type ids.
using ColumnType = ygg::TypeId;

/// Borrowed relation views use a relation repository only as their context.
const ygg::database::RelationRepository& borrowed_relation_context();

void bind_database_module_definitions(nb::module_& m);
void bind_database_distance(nb::module_& m);
void bind_database_evaluation(nb::module_& m);
void bind_database_optimization(nb::module_& m);
void bind_database_queries(nb::module_& m);

}  // namespace yggdrasil

#endif
