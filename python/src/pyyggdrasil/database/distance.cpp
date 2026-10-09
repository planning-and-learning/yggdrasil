#include "distance.hpp"

#include "yggdrasil/database/distance.hpp"
#include "yggdrasil/database/incremental/distance.hpp"
#include "yggdrasil/database/relation.hpp"

#include <cstddef>
#include <span>

namespace yggdrasil
{
namespace nb = nanobind;

namespace
{
using Relation = ygg::Builder<ygg::database::Relation<>>;
using RelationView = ygg::database::RelationView<>;
using Row = ygg::database::Row<>;
using ColumnIndices = std::span<const ygg::database::ColumnLayout>;
using Plan = ygg::database::DistancePlan<>;
using Evaluator = ygg::database::incremental::DistanceEvaluator<>;
using Delta = ygg::database::incremental::Delta<>;

struct BorrowedRelation
{
    const Relation* value;
};

// Dispatch individual reads so mixed Python inputs share one algorithm instantiation.
class RelationInput
{
    const Relation* m_builder = nullptr;
    const RelationView* m_view = nullptr;

    template<typename F>
    decltype(auto) visit(F&& function) const
    {
        if (m_builder)
            return function(*m_builder);
        return function(*m_view);
    }

public:
    explicit RelationInput(nb::handle object)
    {
        if (nb::isinstance<Relation>(object))
            m_builder = &nb::cast<const Relation&>(object);
        else if (nb::isinstance<RelationView>(object))
            m_view = &nb::cast<const RelationView&>(object);
        else if (nb::isinstance<BorrowedRelation>(object))
            m_builder = nb::cast<const BorrowedRelation&>(object).value;
        else
            throw nb::type_error("Expected Relation, RelationView, or BorrowedRelation.");
    }

    auto columns() const
    {
        return visit([this](const auto& relation) { return ygg::make_view(relation.columns().get_data(), *this); });
    }
    size_t arity() const
    {
        return visit([](const auto& relation) { return relation.arity(); });
    }
    size_t size() const
    {
        return visit([](const auto& relation) { return relation.size(); });
    }
    bool empty() const
    {
        return visit([](const auto& relation) { return relation.empty(); });
    }
    const void* get_storage_address() const
    {
        return visit([](const auto& relation) { return relation.get_storage_address(); });
    }
    size_t get_storage_index() const
    {
        return visit([](const auto& relation) { return relation.get_storage_index(); });
    }
    size_t column_index(ygg::Index<ygg::database::Column> column) const
    {
        return visit([column](const auto& relation) { return relation.column_index(column); });
    }
    std::span<const std::byte> row(size_t index) const
    {
        return visit([index](const auto& relation) { return relation.row(index); });
    }
    bool contains(std::span<const std::byte> row) const
    {
        return visit([row](const auto& relation) { return relation.contains(row); });
    }
    bool contains(Row row) const
    {
        return visit([row](const auto& relation) { return relation.contains(row); });
    }
};

static_assert(ygg::database::RelationViewConcept<RelationInput, ygg::database::DefaultColumnTypes>);
}  // namespace

void bind_database_distance(nb::module_& m)
{
    nb::class_<Plan>(m, "DistancePlan", "Validated schemas for directed, unweighted distances between endpoint tuples.")
        .def(nb::new_([](ColumnIndices source_columns, ColumnIndices edge_columns, ColumnIndices target_columns, ygg::uint_t distance_column)
                      { return new Plan(source_columns, edge_columns, target_columns, ygg::Index<ygg::database::Column>(distance_column)); }),
             nb::arg("source_columns"),
             nb::arg("edge_columns"),
             nb::arg("target_columns"),
             nb::arg("distance_column"))
        .def("arity", &Plan::arity)
        .def("output_columns", [](const Plan& plan) { return plan.output_columns().span(); }, nb::keep_alive<0, 1>());

    nb::class_<BorrowedRelation>(m,
                                 "BorrowedRelation",
                                 "Read-only evaluator relation. Retains its owner; the next evaluator initialize or update invalidates borrowed rows.")
        .def("__len__", [](BorrowedRelation relation) { return relation.value->size(); })
        .def(
            "__getitem__",
            [](BorrowedRelation relation, std::ptrdiff_t index)
            {
                if (index < 0)
                    index += static_cast<std::ptrdiff_t>(relation.value->size());
                if (index < 0 || static_cast<size_t>(index) >= relation.value->size())
                    throw nb::index_error();
                return relation.value->at(static_cast<size_t>(index));
            },
            nb::keep_alive<0, 1>())
        .def("arity", [](BorrowedRelation relation) { return relation.value->arity(); })
        .def("empty", [](BorrowedRelation relation) { return relation.value->empty(); })
        .def(
            "at",
            [](BorrowedRelation relation, size_t index) { return relation.value->at(index); },
            nb::arg("index"),
            nb::keep_alive<0, 1>())
        .def("columns", [](BorrowedRelation relation) { return relation.value->columns().span(); }, nb::keep_alive<0, 1>());

    nb::class_<Delta>(m, "RelationDelta", "Read-only added and removed rows from the last evaluator update. The next update replaces them.")
        .def_prop_ro(
            "added",
            [](const Delta& delta) { return BorrowedRelation { &delta.added }; },
            nb::keep_alive<0, 1>())
        .def_prop_ro("removed", [](const Delta& delta) { return BorrowedRelation { &delta.removed }; }, nb::keep_alive<0, 1>());

    m.def(
        "distance",
        [](nb::handle source, nb::handle edges, nb::handle target, const Plan& plan)
        { return ygg::database::distance<>(RelationInput(source), RelationInput(edges), RelationInput(target), plan); },
        nb::arg("source"),
        nb::arg("edges"),
        nb::arg("target"),
        nb::arg("plan"),
        "Return an independent relation of reachable source/target tuples and their shortest distance, including zero-length paths.");

    nb::class_<Evaluator>(m, "DistanceEvaluator", "Maintains distances and exact output deltas. Input relations are borrowed only during each call.")
        .def(nb::init<Plan>(), nb::arg("plan"))
        .def(
            "initialize",
            [](Evaluator& evaluator, nb::handle source, nb::handle edges, nb::handle target)
            { evaluator.initialize(RelationInput(source), RelationInput(edges), RelationInput(target)); },
            nb::arg("source"),
            nb::arg("edges"),
            nb::arg("target"))
        .def(
            "update",
            [](Evaluator& evaluator,
               nb::handle source_added,
               nb::handle source_removed,
               nb::handle edges_added,
               nb::handle edges_removed,
               nb::handle target_added,
               nb::handle target_removed)
            {
                evaluator.update(RelationInput(source_added),
                                 RelationInput(source_removed),
                                 RelationInput(edges_added),
                                 RelationInput(edges_removed),
                                 RelationInput(target_added),
                                 RelationInput(target_removed));
            },
            nb::arg("source_added"),
            nb::arg("source_removed"),
            nb::arg("edges_added"),
            nb::arg("edges_removed"),
            nb::arg("target_added"),
            nb::arg("target_removed"))
        .def(
            "get_result",
            [](const Evaluator& evaluator) { return BorrowedRelation { &evaluator.get_result() }; },
            nb::keep_alive<0, 1>())
        .def(
            "get_delta",
            [](const Evaluator& evaluator) -> const Delta& { return evaluator.get_delta(); },
            nb::rv_policy::reference_internal)
        .def("memory_usage", &Evaluator::memory_usage);
}

}  // namespace yggdrasil
