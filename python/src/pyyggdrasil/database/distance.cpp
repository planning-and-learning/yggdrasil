#include "distance.hpp"
#include "module.hpp"

#include "yggdrasil/database/semantics/distance.hpp"
#include "yggdrasil/database/semantics/incremental/distance.hpp"
#include "yggdrasil/database/semantics/relation.hpp"

#include <nanobind/stl/pair.h>
#include <span>
#include <utility>

namespace yggdrasil
{
namespace nb = nanobind;

namespace
{
using Relation = ygg::Builder<ygg::database::Relation<>>;
using Borrowed = ygg::database::BorrowedRelationView<>;
using Interned = ygg::database::RelationView<>;
using Column = ygg::Index<ygg::database::Column>;
using ColumnIndices = std::span<const ygg::database::ColumnLayout>;
using Plan = ygg::database::DistancePlan<>;
using Evaluator = ygg::database::incremental::DistanceEvaluator<>;
using Delta = ygg::database::incremental::Delta<>;
Borrowed borrow(const Relation& relation) { return Borrowed(relation, borrowed_relation_context()); }

template<typename Input>
Relation distance(const Input& sources, const Input& edges, const Input& targets, const Plan& plan)
{
    return ygg::database::distance<>(sources, edges, targets, plan);
}

/// Each argument is an (added, removed) pair of actual set changes.
template<typename Input>
void update(Evaluator& evaluator, const std::pair<Input, Input>& sources, const std::pair<Input, Input>& edges, const std::pair<Input, Input>& targets)
{
    evaluator.update(sources, edges, targets);
}
}  // namespace

void bind_database_distance(nb::module_& m)
{
    nb::class_<Plan>(m, "DistancePlan", "Validated schemas for directed, unweighted distances between endpoint tuples.")
        .def(nb::init<ColumnIndices, ColumnIndices, ColumnIndices, Column>(),
             nb::arg("source_columns"),
             nb::arg("edge_columns"),
             nb::arg("target_columns"),
             nb::arg("distance_column"))
        .def("arity", &Plan::arity)
        .def("output_columns", [](const Plan& plan) { return plan.output_columns().span(); }, nb::keep_alive<0, 1>());

    nb::class_<Delta>(m, "RelationDelta", "Read-only added and removed rows from the last evaluator update. The next update replaces them.")
        .def_prop_ro("added", [](const Delta& delta) { return borrow(delta.added); }, nb::keep_alive<0, 1>())
        .def_prop_ro("removed", [](const Delta& delta) { return borrow(delta.removed); }, nb::keep_alive<0, 1>());

    constexpr auto distance_doc =
        "Return an independent relation of reachable source/target tuples and their shortest distance, including zero-length paths.";
    m.def("distance", &distance<Borrowed>, nb::arg("source"), nb::arg("edges"), nb::arg("target"), nb::arg("plan"), distance_doc);
    m.def("distance", &distance<Interned>, nb::arg("source"), nb::arg("edges"), nb::arg("target"), nb::arg("plan"), distance_doc);

    nb::class_<Evaluator>(m, "DistanceEvaluator", "Maintains distances and exact output deltas. Input relations are borrowed only during each call.")
        .def(nb::init<Plan>(), nb::arg("plan"))
        .def("initialize", &Evaluator::initialize<Borrowed, Borrowed, Borrowed>, nb::arg("source"), nb::arg("edges"), nb::arg("target"))
        .def("initialize", &Evaluator::initialize<Interned, Interned, Interned>, nb::arg("source"), nb::arg("edges"), nb::arg("target"))
        .def("update", &update<Borrowed>, nb::arg("sources"), nb::arg("edges"), nb::arg("targets"))
        .def("update", &update<Interned>, nb::arg("sources"), nb::arg("edges"), nb::arg("targets"))
        .def(
            "get_result",
            [](const Evaluator& evaluator) { return borrow(evaluator.get_result()); },
            nb::keep_alive<0, 1>(),
            "Borrowed result; the next initialize or update invalidates it.")
        .def(
            "get_delta",
            [](const Evaluator& evaluator) -> const Delta& { return evaluator.get_delta(); },
            nb::rv_policy::reference_internal)
        .def("memory_usage", &Evaluator::memory_usage);
}

}  // namespace yggdrasil
