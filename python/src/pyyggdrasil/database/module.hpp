#ifndef PYYGGDRASIL_DATABASE_MODULE_HPP_
#define PYYGGDRASIL_DATABASE_MODULE_HPP_

#include "yggdrasil/database/semantics/relation.hpp"

#include <cstddef>
#include <cstdint>
#include <nanobind/nanobind.h>

namespace yggdrasil
{
namespace nb = nanobind;

using DatabaseValues = ygg::database::DefaultColumnTypes;

/// Python names of the registered column type ordinals.
enum class ColumnType : size_t
{
    UINT32 = ygg::database::column_type<DatabaseValues, std::uint32_t>,
    INT32 = ygg::database::column_type<DatabaseValues, std::int32_t>,
    UINT64 = ygg::database::column_type<DatabaseValues, std::uint64_t>,
    INT64 = ygg::database::column_type<DatabaseValues, std::int64_t>,
    FLOAT32 = ygg::database::column_type<DatabaseValues, float>,
    FLOAT64 = ygg::database::column_type<DatabaseValues, double>,
    BOOL = ygg::database::column_type<DatabaseValues, bool>,
};

/// Borrowed relation views use a relation repository only as their context.
const ygg::database::RelationRepository<>& borrowed_relation_context();

void bind_database_module_definitions(nb::module_& m);
void bind_database_distance(nb::module_& m);
void bind_database_evaluation(nb::module_& m);
void bind_database_optimization(nb::module_& m);
void bind_database_queries(nb::module_& m);

}  // namespace yggdrasil

#endif
