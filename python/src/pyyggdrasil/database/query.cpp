#include "query.hpp"

#include "query_inputs.hpp"
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
using database_python::BorrowedRelation;
using database_python::Relation;
using database_python::RelationInput;
using database_python::column_labels;

std::vector<Query> roots(nb::handle value)
{
    if (nb::isinstance<Query>(value))
        return { nb::cast<Query>(value) };
    return nb::cast<std::vector<Query>>(value);
}

// Unbound (None) slots borrow the empty relation, so Inputs must not move.
struct Inputs
{
    Relation absent;
    std::vector<RelationInput> relations;

    explicit Inputs(nb::sequence values)
    {
        relations.reserve(nb::len(values));
        for (nb::handle value : values)
            relations.push_back(value.is_none() ? RelationInput(absent) : RelationInput(value));
    }
    Inputs(const Inputs&) = delete;
    Inputs& operator=(const Inputs&) = delete;
};

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
Query relabel(Repository& repository, Query query, const std::vector<ygg::uint_t>& labels)
{
    auto data = db::checkout<db::Query<Values, Tag>>(builder());
    data->arg = require(repository, query);
    const auto indices = column_labels(labels);
    data->labels.set(indices.begin(), indices.end());
    return db::insert_query(repository, builder(), *data);
}

Query select_equal(Repository& repository, Query query, ygg::uint_t left, ygg::uint_t right)
{
    auto data = db::checkout<db::Query<Values, db::QuerySelectEqualTag>>(builder());
    data->arg = require(repository, query);
    data->lhs_column = Column(left);
    data->rhs_column = Column(right);
    return db::insert_query(repository, builder(), *data);
}

Query distance(Repository& repository, Query sources, Query edges, Query targets, ygg::uint_t distance_column)
{
    auto data = db::checkout<db::Query<Values, db::QueryDistanceTag>>(builder());
    data->sources = require(repository, sources);
    data->edges = require(repository, edges);
    data->targets = require(repository, targets);
    data->distance_column = Column(distance_column);
    return db::insert_query(repository, builder(), *data);
}

Query select_value(Repository& repository, Query query, ygg::uint_t label, nb::handle value)
{
    const auto schema = query.columns();
    for (const auto& column : schema)
        if (column.label == Column(label))
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
                                                     data->column = Column(label);
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
            [](Repository& repository, size_t slot, const std::vector<ygg::uint_t>& labels)
            {
                const ygg::Builder<db::Columns<Values>> schema(column_labels(labels));
                return input(repository, slot, schema.span());
            },
            nb::arg("slot"),
            nb::arg("columns"),
            nb::keep_alive<0, 1>())
        .def("empty", &empty, nb::arg("columns"), nb::keep_alive<0, 1>())
        .def(
            "empty",
            [](Repository& repository, const std::vector<ygg::uint_t>& labels)
            {
                const ygg::Builder<db::Columns<Values>> schema(column_labels(labels));
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
            [](const Repository& repository, nb::handle root_queries)
            {
                const auto input = roots(root_queries);
                for (const auto root : input)
                    require(repository, root);
                return db::compile(std::span<const Query>(input));
            },
            nb::arg("roots"));

    nb::class_<Plan>(m, "QueryPlan", "Owns immutable query metadata; independent of the query repository.")
        .def_prop_ro("node_count", [](const Plan& plan) { return plan.node_count(); })
        .def_prop_ro("root_count", [](const Plan& plan) { return plan.root_count(); })
        .def("explain", [](const Plan& plan) { return db::explain(plan); });

    // Python keys distinct counts by plain column labels.
    const auto to_columns = [](const ygg::UnorderedMap<ygg::uint_t, double>& labels)
    {
        ygg::UnorderedMap<Column, double> result;
        for (const auto& [label, count] : labels)
            result.emplace(Column(label), count);
        return result;
    };
    nb::class_<db::RelationStatistics>(m, "RelationStatistics")
        .def(nb::new_([to_columns](double rows, const ygg::UnorderedMap<ygg::uint_t, double>& distinct)
                      { return new db::RelationStatistics { rows, to_columns(distinct) }; }),
             nb::arg("rows") = 0,
             nb::arg("distinct") = ygg::UnorderedMap<ygg::uint_t, double> {})
        .def_rw("rows", &db::RelationStatistics::rows)
        .def_prop_rw(
            "distinct",
            [](const db::RelationStatistics& statistics)
            {
                ygg::UnorderedMap<ygg::uint_t, double> result;
                for (const auto& [column, count] : statistics.distinct)
                    result.emplace(column.get_value(), count);
                return result;
            },
            [to_columns](db::RelationStatistics& statistics, const ygg::UnorderedMap<ygg::uint_t, double>& distinct) { statistics.distinct = to_columns(distinct); });
    using QueryIndex = ygg::Index<db::Query<Values>>;
    nb::class_<db::Statistics<Values>>(m, "Statistics", "What is known about the data; everything is optional.")
        .def(nb::new_(
                 [](std::optional<size_t> objects, ygg::UnorderedMap<size_t, db::RelationStatistics> inputs, ygg::UnorderedMap<QueryIndex, db::RelationStatistics> expressions)
                 { return new db::Statistics<Values> { objects, std::move(inputs), std::move(expressions) }; }),
             nb::arg("objects") = nb::none(),
             nb::arg("inputs") = ygg::UnorderedMap<size_t, db::RelationStatistics> {},
             nb::arg("expressions") = ygg::UnorderedMap<QueryIndex, db::RelationStatistics> {})
        .def_rw("objects", &db::Statistics<Values>::objects)
        .def_rw("inputs", &db::Statistics<Values>::inputs)
        .def_rw("expressions", &db::Statistics<Values>::expressions);

    m.def(
        "optimize",
        [](nb::handle queries, const std::optional<db::Statistics<Values>>& statistics)
        {
            const auto values = roots(queries);
            return db::optimize(std::span<const Query>(values), statistics.value_or(db::Statistics<Values> {}));
        },
        nb::arg("roots"),
        nb::arg("statistics") = nb::none(),
        "Plan from structure, and from statistics for join blocks whose inputs are all measured.");
    m.def(
        "collect_statistics",
        [](nb::sequence values)
        {
            const Inputs input(values);
            return db::collect_statistics<Values>(input.relations);
        },
        nb::arg("inputs"));
    m.def(
        "snapshot",
        [](nb::handle value)
        {
            const RelationInput input(value);
            Relation result(input.columns().span());
            db::assign(result, input);
            return result;
        },
        nb::arg("relation"),
        "Copy a relation or borrowed evaluator result into independent mutable storage.");

    nb::class_<FullEvaluator>(m, "QueryEvaluator", "Evaluate a compiled query DAG. Input relations are borrowed only during each call.")
        .def(nb::init<Plan>(), nb::arg("plan"))
        .def(
            "evaluate",
            [](FullEvaluator& evaluator, nb::sequence values)
            {
                const Inputs input(values);
                evaluator.evaluate(input.relations);
            },
            nb::arg("inputs"))
        .def(
            "get_result",
            [](const FullEvaluator& evaluator, size_t root) { return BorrowedRelation { &evaluator.get_result(root) }; },
            nb::arg("root") = 0,
            nb::keep_alive<0, 1>())
        .def("memory_usage", &FullEvaluator::memory_usage);
    nb::class_<IncrementalEvaluator>(m, "IncrementalQueryEvaluator", "Maintain a compiled query DAG and exact net root deltas.")
        .def(nb::init<Plan>(), nb::arg("plan"))
        .def(
            "initialize",
            [](IncrementalEvaluator& evaluator, nb::sequence values)
            {
                const Inputs input(values);
                evaluator.initialize(input.relations);
            },
            nb::arg("inputs"))
        .def(
            "update",
            [](IncrementalEvaluator& evaluator, nb::sequence added, nb::sequence removed)
            {
                if (nb::len(added) != nb::len(removed))
                    throw std::invalid_argument("Query update: added and removed must have equal input-slot counts.");
                const Inputs additions(added), removals(removed);
                std::vector<std::pair<RelationInput, RelationInput>> deltas;
                deltas.reserve(additions.relations.size());
                for (size_t i = 0; i < additions.relations.size(); ++i)
                    deltas.emplace_back(additions.relations[i], removals.relations[i]);
                evaluator.update(deltas);
            },
            nb::arg("added"),
            nb::arg("removed"))
        .def(
            "get_result",
            [](const IncrementalEvaluator& evaluator, size_t root) { return BorrowedRelation { &evaluator.get_result(root) }; },
            nb::arg("root") = 0,
            nb::keep_alive<0, 1>())
        .def(
            "get_delta",
            [](const IncrementalEvaluator& evaluator, size_t root) -> const db::incremental::Delta<Values>& { return evaluator.get_delta(root); },
            nb::arg("root") = 0,
            nb::rv_policy::reference_internal)
        .def("memory_usage", &IncrementalEvaluator::memory_usage);
}
}  // namespace yggdrasil
