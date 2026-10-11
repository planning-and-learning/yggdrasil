#ifndef PYYGGDRASIL_DATABASE_QUERY_BINDINGS_HPP_
#define PYYGGDRASIL_DATABASE_QUERY_BINDINGS_HPP_

#include "../module.hpp"

#include "yggdrasil/database/syntax/query.hpp"
#include "yggdrasil/python/bindings.hpp"
#include "yggdrasil/python/type_casters.hpp"

#include <nanobind/nanobind.h>
#include <nanobind/stl/vector.h>

namespace yggdrasil::database_python
{
namespace nb = nanobind;

using Values = DatabaseValues;
using Column = ygg::Index<ygg::database::Column>;
using ColumnIndices = std::span<const ygg::database::ColumnLayout>;
using QueryView = ygg::database::QueryView<Values>;
using QueryRepositoryBinding = nb::class_<ygg::database::QueryRepository<Values>>;

template<typename Tag>
using QueryData = ygg::Data<ygg::database::Query<Values, Tag>>;
template<typename Tag>
using ConcreteQueryView = ygg::database::QueryView<Values, Tag>;

void bind_query(nb::module_& m, QueryRepositoryBinding& repository);
void bind_query_input(nb::module_& m, QueryRepositoryBinding& repository);
void bind_query_empty(nb::module_& m, QueryRepositoryBinding& repository);
void bind_query_join(nb::module_& m, QueryRepositoryBinding& repository);
void bind_query_project(nb::module_& m, QueryRepositoryBinding& repository);
void bind_query_rename(nb::module_& m, QueryRepositoryBinding& repository);
void bind_query_select_equal(nb::module_& m, QueryRepositoryBinding& repository);
void bind_query_select_value(nb::module_& m, QueryRepositoryBinding& repository);
void bind_query_union(nb::module_& m, QueryRepositoryBinding& repository);
void bind_query_difference(nb::module_& m, QueryRepositoryBinding& repository);
void bind_query_distance(nb::module_& m, QueryRepositoryBinding& repository);
void bind_query_generic_join(nb::module_& m, QueryRepositoryBinding& repository);

}  // namespace yggdrasil::database_python

#endif
