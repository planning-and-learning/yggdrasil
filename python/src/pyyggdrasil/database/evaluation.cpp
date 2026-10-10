#include "module.hpp"

#include "yggdrasil/database/optimization/optimization.hpp"
#include "yggdrasil/database/semantics/incremental/query.hpp"
#include "yggdrasil/database/semantics/query_evaluation.hpp"

#include <nanobind/stl/pair.h>
#include <nanobind/stl/vector.h>
#include <utility>
#include <vector>

namespace yggdrasil
{
namespace db = ygg::database;

namespace
{
using Values = DatabaseValues;
using FullEvaluator = db::QueryEvaluator<Values>;
using IncrementalEvaluator = db::incremental::QueryEvaluator<Values>;

/// Binds the evaluator members for one input relation type.
template<typename Input>
void bind_inputs(nb::class_<FullEvaluator>& full, nb::class_<IncrementalEvaluator>& incremental, nb::module_& m)
{
    using Inputs = std::vector<Input>;
    using Changes = std::vector<std::pair<Input, Input>>;
    full.def("evaluate", &FullEvaluator::template evaluate<Inputs>, nb::arg("inputs"));
    incremental.def("initialize", &IncrementalEvaluator::template initialize<Inputs>, nb::arg("inputs"))
        .def("update", &IncrementalEvaluator::template update<Changes>, nb::arg("changes"), "One (added, removed) pair per input slot.");
    m.def("collect_statistics", &db::collect_statistics<Values, Inputs>, nb::arg("inputs"));
}
}  // namespace

void bind_database_evaluation(nb::module_& m)
{
    using Borrowed = db::BorrowedRelationView<Values>;
    const auto borrow = [](const ygg::Builder<db::Relation<Values>>& relation) { return Borrowed(relation, borrowed_relation_context()); };

    auto full = nb::class_<FullEvaluator>(m, "QueryEvaluator", "Evaluate a compiled query DAG. Input relations are borrowed only during each call.");
    full.def(nb::init<db::QueryPlan<Values>>(), nb::arg("plan"))
        .def(
            "get_result",
            [borrow](const FullEvaluator& evaluator, size_t root) { return borrow(evaluator.get_result(root)); },
            nb::arg("root") = 0,
            nb::keep_alive<0, 1>(),
            "Borrowed result; the next evaluate invalidates it.")
        .def("memory_usage", &FullEvaluator::memory_usage);

    auto incremental = nb::class_<IncrementalEvaluator>(m, "IncrementalQueryEvaluator", "Maintain a compiled query DAG and exact net root deltas.");
    incremental.def(nb::init<db::QueryPlan<Values>>(), nb::arg("plan"))
        .def(
            "get_result",
            [borrow](const IncrementalEvaluator& evaluator, size_t root) { return borrow(evaluator.get_result(root)); },
            nb::arg("root") = 0,
            nb::keep_alive<0, 1>(),
            "Borrowed result; the next initialize or update invalidates it.")
        .def(
            "get_delta",
            [](const IncrementalEvaluator& evaluator, size_t root) -> const db::incremental::Delta<Values>& { return evaluator.get_delta(root); },
            nb::arg("root") = 0,
            nb::rv_policy::reference_internal)
        .def("memory_usage", &IncrementalEvaluator::memory_usage);

    bind_inputs<Borrowed>(full, incremental, m);
    bind_inputs<db::RelationView<Values>>(full, incremental, m);
}
}  // namespace yggdrasil
