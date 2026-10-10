#include "query.hpp"
#include "module.hpp"

#include "yggdrasil/database/syntax/formatter.hpp"
#include "yggdrasil/database/semantics/incremental/query.hpp"
#include "yggdrasil/database/optimization/optimization.hpp"
#include "yggdrasil/database/semantics/query_evaluation.hpp"
#include "yggdrasil/database/syntax/query.hpp"
#include "yggdrasil/python/bindings.hpp"
#include "yggdrasil/python/type_casters/unordered_map.hpp"

#include <array>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <optional>
#include <ranges>
#include <tuple>
#include <span>
#include <utility>
#include <vector>

namespace yggdrasil
{
namespace nb = nanobind;
namespace db = ygg::database;

namespace
{
using Values = db::DefaultColumnTypes;
using Query = db::QueryView<Values>;
using Repository = db::QueryRepository<Values>;
using Plan = db::QueryPlan<Values>;
using FullEvaluator = db::QueryEvaluator<Values>;
using IncrementalEvaluator = db::incremental::QueryEvaluator<Values>;
using Column = ygg::Index<db::Column>;
using ColumnIndices = std::span<const db::ColumnLayout>;
using Relation = ygg::Builder<db::Relation<Values>>;
using Borrowed = db::BorrowedRelationView<Values>;
using Interned = db::RelationView<Values>;
using Statistics = db::Statistics<Values>;

/// The relations bound to input slots; None is allowed only for slots the plan does not read.
auto relations(const std::vector<std::optional<Borrowed>>& values)
{
    static const Relation empty;
    static const Borrowed absent(empty, borrowed_relation_context());
    return values | std::views::transform([](const std::optional<Borrowed>& value) -> const Borrowed& { return value ? *value : absent; });
}

/// Interned inputs are bound as they are; lists are homogeneous.
const std::vector<Interned>& relations(const std::vector<Interned>& values) { return values; }

Borrowed borrow(const Relation& relation) { return Borrowed(relation, borrowed_relation_context()); }

template<typename Input>
void evaluate(FullEvaluator& evaluator, const std::vector<Input>& inputs)
{
    evaluator.evaluate(relations(inputs));
}

template<typename Input>
void initialize(IncrementalEvaluator& evaluator, const std::vector<Input>& inputs)
{
    evaluator.initialize(relations(inputs));
}

template<typename Input>
void update(IncrementalEvaluator& evaluator, const std::vector<Input>& added, const std::vector<Input>& removed)
{
    if (added.size() != removed.size())
        throw std::invalid_argument("Query update: added and removed must have equal input-slot counts.");
    const auto& additions = relations(added);
    const auto& removals = relations(removed);
    using View = std::ranges::range_reference_t<decltype(additions)>;
    std::vector<std::tuple<View, View>> changes;
    changes.reserve(added.size());
    for (size_t i = 0; i < added.size(); ++i)
        changes.emplace_back(additions[i], removals[i]);
    evaluator.update(changes);
}

template<typename Input>
Statistics collect_statistics(const std::vector<Input>& inputs)
{
    return db::collect_statistics<Values>(relations(inputs));
}

template<db::RelationViewConcept<Values> V>
Relation snapshot(const V& relation)
{
    Relation result(relation.columns().span());
    db::assign(result, relation);
    return result;
}

db::QueryRepositoryFactory<Values>& factory()
{
    static db::QueryRepositoryFactory<Values> instance;
    return instance;
}

db::QueryBuilder<Values>& builder()
{
    static db::QueryBuilder<Values> instance;
    return instance;
}

ygg::Index<db::Query<Values>> require(const Repository& repository, Query query)
{
    if (&query.get_repository() != &repository)
        throw std::invalid_argument("Query: operand belongs to another repository.");
    return query.get_index();
}

Query input(Repository& repository, size_t slot, ColumnIndices schema)
{
    auto data = db::checkout<db::Query<Values, db::QueryInputTag>>(builder());
    data->input_slot = slot;
    data->columns.set(schema.begin(), schema.end());
    return db::insert_query(repository, builder(), *data);
}

Query empty(Repository& repository, ColumnIndices schema)
{
    auto data = db::checkout<db::Query<Values, db::QueryEmptyTag>>(builder());
    data->columns.set(schema.begin(), schema.end());
    return db::insert_query(repository, builder(), *data);
}

template<typename Tag>
Query binary(Repository& repository, Query lhs, Query rhs)
{
    auto data = db::checkout<db::Query<Values, Tag>>(builder());
    data->lhs = require(repository, lhs);
    data->rhs = require(repository, rhs);
    return db::insert_query(repository, builder(), *data);
}

template<typename Tag>
Query relabel(Repository& repository, Query query, const std::vector<Column>& labels)
{
    auto data = db::checkout<db::Query<Values, Tag>>(builder());
    data->arg = require(repository, query);
    data->labels.set(labels.begin(), labels.end());
    return db::insert_query(repository, builder(), *data);
}

Query select_equal(Repository& repository, Query query, Column left, Column right)
{
    auto data = db::checkout<db::Query<Values, db::QuerySelectEqualTag>>(builder());
    data->arg = require(repository, query);
    data->lhs_column = left;
    data->rhs_column = right;
    return db::insert_query(repository, builder(), *data);
}

Query distance(Repository& repository, Query sources, Query edges, Query targets, Column distance_column)
{
    auto data = db::checkout<db::Query<Values, db::QueryDistanceTag>>(builder());
    data->sources = require(repository, sources);
    data->edges = require(repository, edges);
    data->targets = require(repository, targets);
    data->distance_column = distance_column;
    return db::insert_query(repository, builder(), *data);
}

Query select_value(Repository& repository, Query query, Column label, nb::handle value)
{
    for (const auto& column : query.columns())
        if (column.label == label)
            return db::visit_column_type<Values>(column.type,
                                                 [&]<typename T>(std::type_identity<T>)
                                                 {
                                                     T converted;
                                                     if (!nb::try_cast(value, converted))
                                                         throw nb::type_error("Query selection: value does not match the column type.");
                                                     std::array<std::byte, db::ColumnCodec<T>::size> bytes;
                                                     db::ColumnCodec<T>::encode(converted, bytes);
                                                     auto data = db::checkout<db::Query<Values, db::QuerySelectValueTag>>(builder());
                                                     data->arg = require(repository, query);
                                                     data->column = label;
                                                     data->constant.set(bytes.begin(), bytes.end());
                                                     return db::insert_query(repository, builder(), *data);
                                                 });
    throw std::out_of_range("Query selection: unknown column.");
}
}  // namespace

void bind_database_query(nb::module_& m)
{
    ygg::bind_index<ygg::Index<db::Query<Values>>>(m, "QueryIndex");
    auto query = nb::class_<Query>(m, "Query", "Immutable interned query. Keeps its repository alive.")
                     .def("get_index", &Query::get_index)
                     .def("columns", &Query::columns, nb::keep_alive<0, 1>());
    ygg::add_comparison(query);
    ygg::add_hash(query);

    nb::class_<Repository>(m, "QueryRepository", "Interns validated relational expressions without reading data.")
        .def(nb::new_([] { return new Repository(factory().create()); }))
        .def("__len__", [](const Repository& repository) { return repository.size(); })
        .def("input", &input, nb::arg("slot"), nb::arg("columns"), nb::keep_alive<0, 1>())
        .def(
            "input",
            [](Repository& repository, size_t slot, const std::vector<Column>& labels)
            {
                const ygg::Builder<db::Columns<Values>> schema(labels);
                return input(repository, slot, schema.span());
            },
            nb::arg("slot"),
            nb::arg("columns"),
            nb::keep_alive<0, 1>())
        .def("empty", &empty, nb::arg("columns"), nb::keep_alive<0, 1>())
        .def(
            "empty",
            [](Repository& repository, const std::vector<Column>& labels)
            {
                const ygg::Builder<db::Columns<Values>> schema(labels);
                return empty(repository, schema.span());
            },
            nb::arg("columns"),
            nb::keep_alive<0, 1>())
        .def("join", &binary<db::QueryJoinTag>, nb::arg("left"), nb::arg("right"), nb::keep_alive<0, 1>())
        .def("project", &relabel<db::QueryProjectTag>, nb::arg("query"), nb::arg("columns"), nb::keep_alive<0, 1>())
        .def("rename", &relabel<db::QueryRenameTag>, nb::arg("query"), nb::arg("columns"), nb::keep_alive<0, 1>())
        .def("select_equal", &select_equal, nb::arg("query"), nb::arg("left_column"), nb::arg("right_column"), nb::keep_alive<0, 1>())
        .def("select_value", &select_value, nb::arg("query"), nb::arg("column"), nb::arg("value"), nb::keep_alive<0, 1>())
        .def("union", &binary<db::QueryUnionTag>, nb::arg("left"), nb::arg("right"), nb::keep_alive<0, 1>())
        .def("difference", &binary<db::QueryDifferenceTag>, nb::arg("left"), nb::arg("right"), nb::keep_alive<0, 1>())
        .def("distance", &distance, nb::arg("source"), nb::arg("edges"), nb::arg("target"), nb::arg("distance_column"), nb::keep_alive<0, 1>())
        .def(
            "compile",
            [](const Repository& repository, Query root)
            {
                require(repository, root);
                return db::compile(std::span<const Query>(&root, 1));
            },
            nb::arg("root"))
        .def(
            "compile",
            [](const Repository& repository, const std::vector<Query>& roots)
            {
                for (const auto root : roots)
                    require(repository, root);
                return db::compile(std::span<const Query>(roots));
            },
            nb::arg("roots"));

    nb::class_<Plan>(m, "QueryPlan", "Owns immutable query metadata; independent of the query repository.")
        .def_prop_ro("node_count", [](const Plan& plan) { return plan.node_count(); })
        .def_prop_ro("root_count", [](const Plan& plan) { return plan.root_count(); })
        .def("explain", [](const Plan& plan) { return db::explain(plan); });

    using Distinct = ygg::UnorderedMap<Column, double>;
    nb::class_<db::RelationStatistics>(m, "RelationStatistics")
        .def(nb::new_([](double rows, Distinct distinct) { return new db::RelationStatistics { rows, std::move(distinct) }; }),
             nb::arg("rows") = 0,
             nb::arg("distinct") = Distinct {})
        .def_rw("rows", &db::RelationStatistics::rows)
        .def_rw("distinct", &db::RelationStatistics::distinct);
    using QueryIndex = ygg::Index<db::Query<Values>>;
    using InputStatistics = ygg::UnorderedMap<size_t, db::RelationStatistics>;
    using ExpressionStatistics = ygg::UnorderedMap<QueryIndex, db::RelationStatistics>;
    nb::class_<Statistics>(m, "Statistics", "What is known about the data; everything is optional.")
        .def(nb::new_([](std::optional<size_t> objects, InputStatistics inputs, ExpressionStatistics expressions)
                      { return new Statistics { objects, std::move(inputs), std::move(expressions) }; }),
             nb::arg("objects") = nb::none(),
             nb::arg("inputs") = InputStatistics {},
             nb::arg("expressions") = ExpressionStatistics {})
        .def_rw("objects", &Statistics::objects)
        .def_rw("inputs", &Statistics::inputs)
        .def_rw("expressions", &Statistics::expressions);

    constexpr auto optimize_doc = "Plan from structure, and from statistics for join blocks whose inputs are all measured.";
    m.def(
        "optimize",
        [](Query root, const std::optional<Statistics>& statistics) { return db::optimize(root, statistics.value_or(Statistics {})); },
        nb::arg("root"),
        nb::arg("statistics") = nb::none(),
        optimize_doc);
    m.def(
        "optimize",
        [](const std::vector<Query>& roots, const std::optional<Statistics>& statistics)
        { return db::optimize(std::span<const Query>(roots), statistics.value_or(Statistics {})); },
        nb::arg("roots"),
        nb::arg("statistics") = nb::none(),
        optimize_doc);
    m.def("collect_statistics", &collect_statistics<std::optional<Borrowed>>, nb::arg("inputs"));
    m.def("collect_statistics", &collect_statistics<Interned>, nb::arg("inputs"));
    constexpr auto snapshot_doc = "Copy a relation, e.g. a borrowed evaluator result, into independent mutable storage.";
    m.def("snapshot", &snapshot<Borrowed>, nb::arg("relation"), snapshot_doc);
    m.def("snapshot", &snapshot<Interned>, nb::arg("relation"), snapshot_doc);

    nb::class_<FullEvaluator>(m, "QueryEvaluator", "Evaluate a compiled query DAG. Input relations are borrowed only during each call.")
        .def(nb::init<Plan>(), nb::arg("plan"))
        .def("evaluate", &evaluate<std::optional<Borrowed>>, nb::arg("inputs"))
        .def("evaluate", &evaluate<Interned>, nb::arg("inputs"))
        .def(
            "get_result",
            [](const FullEvaluator& evaluator, size_t root) { return borrow(evaluator.get_result(root)); },
            nb::arg("root") = 0,
            nb::keep_alive<0, 1>(),
            "Borrowed result; the next evaluate invalidates it.")
        .def("memory_usage", &FullEvaluator::memory_usage);
    nb::class_<IncrementalEvaluator>(m, "IncrementalQueryEvaluator", "Maintain a compiled query DAG and exact net root deltas.")
        .def(nb::init<Plan>(), nb::arg("plan"))
        .def("initialize", &initialize<std::optional<Borrowed>>, nb::arg("inputs"))
        .def("initialize", &initialize<Interned>, nb::arg("inputs"))
        .def("update", &update<std::optional<Borrowed>>, nb::arg("added"), nb::arg("removed"))
        .def("update", &update<Interned>, nb::arg("added"), nb::arg("removed"))
        .def(
            "get_result",
            [](const IncrementalEvaluator& evaluator, size_t root) { return borrow(evaluator.get_result(root)); },
            nb::arg("root") = 0,
            nb::keep_alive<0, 1>(),
            "Borrowed result; the next initialize or update invalidates it.")
        .def(
            "get_delta",
            [](const IncrementalEvaluator& evaluator, size_t root) -> const db::incremental::Delta<Values>& { return evaluator.get_delta(root); },
            nb::arg("root") = 0,
            nb::rv_policy::reference_internal)
        .def("memory_usage", &IncrementalEvaluator::memory_usage);
}
}  // namespace yggdrasil
