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
#include <span>
#include <vector>

namespace yggdrasil
{

void bind_database_optimization(nb::module_& m)
{
    using Plan = ygg::database::QueryPlan;
    using Roots = std::vector<ygg::database::QueryView<>>;
    using Statistics = ygg::database::Statistics;
    using Column = ygg::Index<ygg::database::Column>;

    nb::class_<Plan>(m, "QueryPlan", "Owns immutable query metadata; independent of the query repository.")
        .def_prop_ro("node_count", &Plan::node_count)
        .def_prop_ro("root_count", &Plan::root_count)
        .def("explain", &ygg::database::explain);
    m.def("compile", &ygg::database::compile<Roots>, nb::arg("roots"), "Copy the queries reachable from the roots into an independent plan.");

    using Distinct = ygg::UnorderedMap<Column, double>;
    nb::class_<ygg::database::RelationStatistics>(m, "RelationStatistics")
        .def(nb::init<double, Distinct>(), nb::arg("rows") = 0, nb::arg("distinct") = Distinct {})
        .def_rw("rows", &ygg::database::RelationStatistics::rows)
        .def_rw("distinct", &ygg::database::RelationStatistics::distinct);
    using InputStatistics = ygg::UnorderedMap<size_t, ygg::database::RelationStatistics>;
    using ExpressionStatistics = ygg::UnorderedMap<ygg::Index<ygg::database::Query<>>, ygg::database::RelationStatistics>;
    nb::class_<Statistics>(m, "Statistics", "What is known about the data; everything is optional.")
        .def(nb::init<std::optional<size_t>, InputStatistics, ExpressionStatistics>(),
             nb::arg("objects") = nb::none(),
             nb::arg("inputs") = InputStatistics {},
             nb::arg("expressions") = ExpressionStatistics {})
        .def_rw("objects", &Statistics::objects)
        .def_rw("inputs", &Statistics::inputs)
        .def_rw("expressions", &Statistics::expressions);

    using Evaluation = ygg::database::Evaluation;
    nb::enum_<Evaluation>(m, "Evaluation", "How a root is evaluated: fully, or bound with some columns given per call.")
        .value("FULL", Evaluation::full)
        .value("BOUND", Evaluation::bound);
    m.def(
        "optimize",
        [](const Roots& roots, const Statistics& statistics, const std::optional<std::vector<Evaluation>>& evaluations)
        {
            const auto modes = evaluations.value_or(std::vector<Evaluation>(roots.size(), Evaluation::full));
            return ygg::database::optimize(std::span<const ygg::database::QueryView<>>(roots), std::span<const Evaluation>(modes), statistics);
        },
        nb::arg("roots"),
        nb::arg("statistics") = Statistics {},
        nb::arg("evaluations") = nb::none(),
        "Plan from structure, and from statistics for join blocks whose inputs are all measured. Roots evaluated BOUND are "
        "only normalized; evaluations defaults to FULL for every root.");
}
}  // namespace yggdrasil
