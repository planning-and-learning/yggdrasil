#include "module.hpp"

#include "yggdrasil/database/optimization/optimization.hpp"
#include "yggdrasil/database/syntax/formatter.hpp"
#include "yggdrasil/database/syntax/query.hpp"
#include "yggdrasil/python/type_casters.hpp"
#include "yggdrasil/python/type_casters/unordered_map.hpp"

#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <optional>
#include <vector>

namespace yggdrasil
{
namespace db = ygg::database;

void bind_database_optimization(nb::module_& m)
{
    using Values = DatabaseValues;
    using Plan = db::QueryPlan<Values>;
    using Roots = std::vector<db::QueryView<Values>>;
    using Statistics = db::Statistics<Values>;
    using Column = ygg::Index<db::Column>;

    nb::class_<Plan>(m, "QueryPlan", "Owns immutable query metadata; independent of the query repository.")
        .def_prop_ro("node_count", &Plan::node_count)
        .def_prop_ro("root_count", &Plan::root_count)
        .def("explain", &db::explain<Values>);
    m.def("compile", &db::compile<Roots>, nb::arg("roots"), "Copy the queries reachable from the roots into an independent plan.");

    using Distinct = ygg::UnorderedMap<Column, double>;
    nb::class_<db::RelationStatistics>(m, "RelationStatistics")
        .def(nb::init<double, Distinct>(), nb::arg("rows") = 0, nb::arg("distinct") = Distinct {})
        .def_rw("rows", &db::RelationStatistics::rows)
        .def_rw("distinct", &db::RelationStatistics::distinct);
    using InputStatistics = ygg::UnorderedMap<size_t, db::RelationStatistics>;
    using ExpressionStatistics = ygg::UnorderedMap<ygg::Index<db::Query<Values>>, db::RelationStatistics>;
    nb::class_<Statistics>(m, "Statistics", "What is known about the data; everything is optional.")
        .def(nb::init<std::optional<size_t>, InputStatistics, ExpressionStatistics>(),
             nb::arg("objects") = nb::none(),
             nb::arg("inputs") = InputStatistics {},
             nb::arg("expressions") = ExpressionStatistics {})
        .def_rw("objects", &Statistics::objects)
        .def_rw("inputs", &Statistics::inputs)
        .def_rw("expressions", &Statistics::expressions);

    m.def("optimize",
          &db::optimize<Values, Roots>,
          nb::arg("roots"),
          nb::arg("statistics") = Statistics {},
          "Plan from structure, and from statistics for join blocks whose inputs are all measured.");
}
}  // namespace yggdrasil
