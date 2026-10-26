#include "module.hpp"
#include "yggdrasil/database/evaluation/full.hpp"
#include "yggdrasil/database/evaluation/incremental.hpp"
#include "yggdrasil/database/optimization/optimization.hpp"

#include <nanobind/stl/pair.h>
#include <nanobind/stl/vector.h>
#include <utility>
#include <vector>

namespace yggdrasil
{

namespace
{
using FullEvaluator = ygg::database::QueryEvaluator;
using IncrementalEvaluator = ygg::database::incremental::QueryEvaluator;

/// Binds the evaluator members for one input relation type.
template<typename Input>
void bind_inputs(nb::class_<FullEvaluator>& full, nb::class_<IncrementalEvaluator>& incremental, nb::module_& m)
{
    using Inputs = std::vector<Input>;
    using Changes = std::vector<std::pair<Input, Input>>;
    full.def("evaluate", &FullEvaluator::template evaluate<Inputs>, nb::arg("inputs"));
    incremental.def("initialize", &IncrementalEvaluator::template initialize<Inputs>, nb::arg("inputs"))
        .def("update", &IncrementalEvaluator::template update<Changes>, nb::arg("changes"), "One (added, removed) pair per input slot.");
    m.def("collect_statistics", &ygg::database::collect_statistics<Inputs>, nb::arg("inputs"));
}
}  // namespace

void bind_database_evaluation(nb::module_& m)
{
    using Borrowed = ygg::database::BorrowedRelationView;
    const auto borrow = [](const ygg::Builder<ygg::database::Relation>& relation) { return Borrowed(relation, borrowed_relation_context()); };

    auto full = nb::class_<FullEvaluator>(m, "QueryEvaluator", "Evaluate a compiled query DAG. Input relations are borrowed only during each call.");
    full.def(nb::init<ygg::database::QueryPlan>(), nb::arg("plan"))
        .def(
            "get_result",
            [borrow](const FullEvaluator& evaluator, size_t root) { return borrow(evaluator.get_result(root)); },
            nb::arg("root") = 0,
            nb::keep_alive<0, 1>(),
            "Borrowed result; the next evaluate invalidates it.")
        .def("memory_usage", &FullEvaluator::memory_usage);

    auto incremental = nb::class_<IncrementalEvaluator>(m, "IncrementalQueryEvaluator", "Maintain a compiled query DAG and exact net root deltas.");
    incremental.def(nb::init<ygg::database::QueryPlan>(), nb::arg("plan"))
        .def(
            "get_result",
            [borrow](const IncrementalEvaluator& evaluator, size_t root) { return borrow(evaluator.get_result(root)); },
            nb::arg("root") = 0,
            nb::keep_alive<0, 1>(),
            "Borrowed result; the next initialize or update invalidates it.")
        .def(
            "get_delta",
            [](const IncrementalEvaluator& evaluator, size_t root) -> const ygg::database::incremental::Delta& { return evaluator.get_delta(root); },
            nb::arg("root") = 0,
            nb::rv_policy::reference_internal)
        .def("memory_usage", &IncrementalEvaluator::memory_usage);

    bind_inputs<Borrowed>(full, incremental, m);
    bind_inputs<ygg::database::RelationView>(full, incremental, m);
}
}  // namespace yggdrasil
