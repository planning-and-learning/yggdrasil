#include "module.hpp"

#include "yggdrasil/database/semantics/distance.hpp"
#include "yggdrasil/database/semantics/incremental/distance.hpp"
#include "yggdrasil/database/semantics/relation.hpp"

#include <nanobind/stl/pair.h>
#include <span>
#include <utility>

namespace yggdrasil
{
namespace db = ygg::database;

namespace
{
using Values = DatabaseValues;
using Relation = ygg::Builder<db::Relation<Values>>;
using Column = ygg::Index<db::Column>;
using ColumnIndices = std::span<const db::ColumnLayout>;
using Plan = db::DistancePlan<Values>;
using Evaluator = db::incremental::DistanceEvaluator<Values>;
using Delta = db::incremental::Delta<Values>;

/// Binds distance and the evaluator members for one input relation type.
template<typename Input>
void bind_inputs(nb::class_<Evaluator>& evaluator, nb::module_& m)
{
    using Change = std::pair<Input, Input>;
    m.def("distance",
          static_cast<Relation (*)(const Input&, const Input&, const Input&, const Plan&)>(&db::distance<Values, Input, Input, Input>),
          nb::arg("sources"),
          nb::arg("edges"),
          nb::arg("targets"),
          nb::arg("plan"),
          "Return an independent relation of reachable source/target tuples and their shortest distance, including zero-length paths.");
    evaluator.def("initialize", &Evaluator::template initialize<Input, Input, Input>, nb::arg("sources"), nb::arg("edges"), nb::arg("targets"))
        .def("update",
             &Evaluator::template update<Change, Change, Change>,
             nb::arg("sources"),
             nb::arg("edges"),
             nb::arg("targets"),
             "Each argument is an (added, removed) pair of actual set changes.");
}
}  // namespace

void bind_database_distance(nb::module_& m)
{
    using Borrowed = db::BorrowedRelationView<Values>;
    const auto borrow = [](const Relation& relation) { return Borrowed(relation, borrowed_relation_context()); };

    nb::class_<Plan>(m, "DistancePlan", "Validated schemas for directed, unweighted distances between endpoint tuples.")
        .def(nb::init<ColumnIndices, ColumnIndices, ColumnIndices, Column>(),
             nb::arg("source_columns"),
             nb::arg("edge_columns"),
             nb::arg("target_columns"),
             nb::arg("distance_column"))
        .def("arity", &Plan::arity)
        .def("output_columns", [](const Plan& plan) { return plan.output_columns().span(); }, nb::keep_alive<0, 1>());

    nb::class_<Delta>(m, "RelationDelta", "Read-only added and removed rows from the last evaluator update. The next update replaces them.")
        .def_prop_ro("added", [borrow](const Delta& delta) { return borrow(delta.added); }, nb::keep_alive<0, 1>())
        .def_prop_ro("removed", [borrow](const Delta& delta) { return borrow(delta.removed); }, nb::keep_alive<0, 1>());

    auto evaluator = nb::class_<Evaluator>(m, "DistanceEvaluator", "Maintains distances and exact output deltas. Input relations are borrowed only during each call.");
    evaluator.def(nb::init<Plan>(), nb::arg("plan"))
        .def(
            "get_result",
            [borrow](const Evaluator& self) { return borrow(self.get_result()); },
            nb::keep_alive<0, 1>(),
            "Borrowed result; the next initialize or update invalidates it.")
        .def(
            "get_delta",
            [](const Evaluator& self) -> const Delta& { return self.get_delta(); },
            nb::rv_policy::reference_internal)
        .def("memory_usage", &Evaluator::memory_usage);

    bind_inputs<Borrowed>(evaluator, m);
    bind_inputs<db::RelationView<Values>>(evaluator, m);
}
}  // namespace yggdrasil
