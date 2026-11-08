#include "module.hpp"
#include "pyyggdrasil/module.hpp"
#include <urd/python/sequences/sequence.hpp>
#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>

#include "yggdrasil/database/semantics/operations.hpp"
#include "yggdrasil/database/semantics/plans.hpp"
#include "yggdrasil/formalism/relation_view.hpp"
#include "yggdrasil/python/bindings.hpp"
#include <urd/python/owner.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/operators.h>
#include <nanobind/stl/vector.h>
#include <initializer_list>
#include <stdexcept>
#include <type_traits>
#include <optional>
#include <span>
#include <vector>

namespace yggdrasil
{

namespace
{
using Column = urd::index<ygg::Column>;

/// A Python sequence index; negative values count from the end.
size_t sequence_index(std::ptrdiff_t index, size_t size)
{
    if (index < 0)
        index += static_cast<std::ptrdiff_t>(size);
    if (index < 0 || static_cast<size_t>(index) >= size)
        throw nb::index_error();
    return static_cast<size_t>(index);
}

/// A row as Python reads it: its stored row (a Row<Schema> view's get_values()) with its relation's columns, which give
/// its values' types.
struct Row
{
    ygg::database::RowPool::row_type row;
    std::span<const ygg::ColumnLayout> columns;
};

template<typename R>
Row row_at(const R& relation, std::ptrdiff_t index)
{ return { relation.row(sequence_index(index, relation.size())).get_values(), relation.columns() }; }

/// Lazy iteration over a sequence binding: map(self.__getitem__, range(len(self))), so every element keeps the
/// lifetime rules of __getitem__.
template<typename Element, typename T>
nb::typed<nb::iterator, Element> iterate(nb::typed<nb::handle, T> self)
{
    const auto builtins = nb::module_::import_("builtins");
    return nb::borrow<nb::typed<nb::iterator, Element>>(builtins.attr("map")(self.attr("__getitem__"), builtins.attr("range")(nb::len(self))));
}

/// The members of every relation class: size, arity and columns.
template<typename R>
void bind_relation_shape(nb::class_<R>& cls)
{
    cls.def("__len__", &R::size)
        .def("arity", [](const R& relation) { return relation.columns().size(); })
        .def("empty", &R::empty)
        .def("columns", [](const R& relation) { return relation.columns(); }, nb::keep_alive<0, 1>());
}

/// The read members of every relation view class: its shape and its rows by position.
template<typename R>
void bind_relation_reads(nb::class_<R>& cls)
{
    bind_relation_shape(cls);
    cls.def("__getitem__", &row_at<R>, nb::keep_alive<0, 1>())
        .def("__iter__", &iterate<Row, R>)
        .def(
            "at",
            [](const R& relation, std::size_t index)
            {
                if (index >= relation.size())
                    throw nb::index_error();
                return Row { relation.row(index).get_values(), relation.columns() };
            },
            nb::arg("index"),
            nb::keep_alive<0, 1>());
}
using Relation = urd::builder<ygg::Relation<ygg::Schema>>;
using ColumnIndices = std::span<const ygg::ColumnLayout>;
using Workspace = ygg::database::Workspace;
using Layouts = urd::Store<urd::sequence<ygg::ColumnLayout>>;

/// Columns of these labels, each of type uint_t.
Layouts untyped(const urd::Store<urd::sequence<Column>>& labels)
{
    auto result = Layouts();
    for (const auto label : labels)
        result.push_back(ygg::ColumnLayout { label, ygg::type_id<ygg::uint_t> });
    return result;
}

/// The plans, built from their inputs' columns (validated there), and the scratch storage of the operations.
void bind_plans(nb::module_& m)
{
    using namespace ygg::database;
    nb::class_<Workspace>(m, "Workspace", "Scratch storage of the relational operations; reuse one, never concurrently.")
        .def(nb::init<>())
        .def("memory_usage", &Workspace::memory_usage);
    nb::class_<ProjectionPlan>(m, "ProjectionPlan", "Keeps the input's columns of these labels, in this order.")
        .def(nb::init<ColumnIndices, urd::Store<urd::sequence<Column>>>(), nb::arg("input"), nb::arg("labels"))
        .def("input_columns", &ProjectionPlan::input_columns, nb::keep_alive<0, 1>())
        .def("output_columns", &ProjectionPlan::output_columns, nb::keep_alive<0, 1>());
    nb::class_<JoinPlan>(m, "JoinPlan", "Natural join (also semijoin, antijoin) of inputs with these columns.")
        .def(nb::init<ColumnIndices, ColumnIndices>(), nb::arg("lhs"), nb::arg("rhs"))
        .def("lhs_columns", &JoinPlan::lhs_columns, nb::keep_alive<0, 1>())
        .def("rhs_columns", &JoinPlan::rhs_columns, nb::keep_alive<0, 1>())
        .def("output_columns", &JoinPlan::output_columns, nb::keep_alive<0, 1>());
    nb::class_<SetPlan>(m, "SetPlan", "Union and difference of inputs with the same columns in the same order.")
        .def(nb::init<ColumnIndices, ColumnIndices>(), nb::arg("lhs"), nb::arg("rhs"))
        .def("output_columns", &SetPlan::output_columns, nb::keep_alive<0, 1>());
    nb::class_<SelectEqualPlan>(m, "SelectEqualPlan", "The rows whose two columns (of one type) hold equal values.")
        .def(nb::init<ColumnIndices, Column, Column>(), nb::arg("input"), nb::arg("lhs"), nb::arg("rhs"))
        .def("output_columns", &SelectEqualPlan::output_columns, nb::keep_alive<0, 1>());
    nb::class_<SelectValuePlan>(m, "SelectValuePlan", "The rows whose column holds a value of the column's type.")
        .def(
            "__init__",
            [](SelectValuePlan* self, ColumnIndices input, Column column, ColumnType type, nb::handle value)
            {
                new (self) SelectValuePlan(input, column, type, ygg::encode(cast_value(value, type)));
            },
            nb::arg("input"),
            nb::arg("column"),
            nb::arg("type"),
            nb::arg("value"))
        .def("output_columns", &SelectValuePlan::output_columns, nb::keep_alive<0, 1>());
}

/// The operations of one input relation type.
template<typename V>
void bind_unary(nb::module_& m)
{
    using namespace ygg::database;
    m.def(
        "project",
        [](const V& input, const ProjectionPlan& plan, Relation& out, Workspace& workspace)
        {
            require_columns(input.columns(), plan.input_columns());
            require_columns(out.columns(), plan.output_columns());
            project(input, plan, out, workspace);
        },
        nb::arg("input"),
        nb::arg("plan"),
        nb::arg("out"),
        nb::arg("workspace"));
    m.def(
        "select_equal_columns",
        [](const V& input, const SelectEqualPlan& plan, Relation& out)
        {
            require_columns(input.columns(), plan.output_columns());
            require_columns(out.columns(), plan.output_columns());
            select_equal_columns(input, plan, out);
        },
        nb::arg("input"),
        nb::arg("plan"),
        nb::arg("out"));
    m.def(
        "select_equal_value",
        [](const V& input, const SelectValuePlan& plan, Relation& out)
        {
            require_columns(input.columns(), plan.output_columns());
            require_columns(out.columns(), plan.output_columns());
            select_equal_value(input, plan, out);
        },
        nb::arg("input"),
        nb::arg("plan"),
        nb::arg("out"));
    m.def(
        "assign",
        [](nb::typed<nb::handle, Relation> destination, const V& source)
        {
            assign(nb::cast<Relation&>(destination), source);
            return destination;
        },
        nb::arg("destination"),
        nb::arg("source"));
}

/// The operations of one pair of input relation types.
template<typename L, typename R>
void bind_binary(nb::module_& m)
{
    using namespace ygg::database;
    const auto bind_join = [&m](const char* name, auto operation, bool output_is_lhs)
    {
        m.def(
            name,
            [operation, output_is_lhs](const L& lhs, const R& rhs, const JoinPlan& plan, Relation& out, Workspace& workspace)
            {
                require_columns(lhs.columns(), plan.lhs_columns());
                require_columns(rhs.columns(), plan.rhs_columns());
                require_columns(out.columns(), output_is_lhs ? plan.lhs_columns() : plan.output_columns());
                operation(lhs, rhs, plan, out, workspace);
            },
            nb::arg("lhs"),
            nb::arg("rhs"),
            nb::arg("plan"),
            nb::arg("out"),
            nb::arg("workspace"));
    };
    bind_join("join", [](auto&&... args) { join(args...); }, false);
    bind_join("semijoin", [](auto&&... args) { semijoin(args...); }, true);
    bind_join("antijoin", [](auto&&... args) { antijoin(args...); }, true);
    const auto bind_set = [&m](const char* name, auto operation)
    {
        m.def(
            name,
            [operation](const L& lhs, const R& rhs, const SetPlan& plan, Relation& out)
            {
                for (const auto columns : { lhs.columns(), rhs.columns(), out.columns() })
                    require_columns(columns, plan.output_columns());
                operation(lhs, rhs, plan, out);
            },
            nb::arg("lhs"),
            nb::arg("rhs"),
            nb::arg("plan"),
            nb::arg("out"));
    };
    bind_set("union", [](auto&&... args) { union_(args...); });
    bind_set("difference", [](auto&&... args) { difference(args...); });
}
}  // namespace

void bind_database_module_definitions(nb::module_& m)
{
    using RelationTag = ygg::Relation<ygg::Schema>;
    using RelationView = ygg::database::RelationView;
    using BuilderRelationView = ygg::database::BuilderRelationView;
    using RelationRepository = ygg::database::Repository;
    urd::python::bind_index<Column>(m, "ColumnIndex");

    nb::enum_<ColumnType>(m, "ColumnType")
        .value("UINT32", ygg::type_id<std::uint32_t>)
        .value("INT32", ygg::type_id<std::int32_t>)
        .value("UINT64", ygg::type_id<std::uint64_t>)
        .value("INT64", ygg::type_id<std::int64_t>)
        .value("FLOAT32", ygg::type_id<float>)
        .value("FLOAT64", ygg::type_id<double>)
        .value("BOOL", ygg::type_id<bool>)
        .value("OBJECT", ygg::type_id<urd::index<ygg::formalism::Object<void>>>);

    nb::class_<Row>(m,
                    "RelationRow",
                    "Read-only borrowed row. Keeps its Python owner alive, but clearing the owning "
                    "relation or repository invalidates the borrowed data. Memoization resets alone do not.")
        .def("__len__", [](const Row& row) { return row.columns.size(); })
        .def("__iter__", &iterate<nb::object, Row>)
        .def("__getitem__",
             [](const Row& row, std::ptrdiff_t index)
             {
                 const auto position = sequence_index(index, row.columns.size());
                 return ygg::decode(row.row.get(position), row.columns[position].type);
             });

    nb::class_<ColumnIndices>(m,
                              "ColumnIndices",
                              "Read-only borrowed column labels. Keeps its Python owner alive; "
                              "replacing a builder's schema or clearing its repository invalidates the labels.")
        .def("__len__", &ColumnIndices::size)
        .def("__iter__", &iterate<Column, ColumnIndices>)
        .def("__getitem__", [](ColumnIndices columns, std::ptrdiff_t index) { return columns[sequence_index(index, columns.size())].label; })
        .def("type",
             [](ColumnIndices columns, size_t index)
             {
                 if (index >= columns.size())
                     throw nb::index_error();
                 return columns[index].type;
             });

    nb::class_<ygg::ColumnLayout>(m, "ColumnLayout", "A column: its label and type.")
        .def(nb::init<Column, ColumnType>(), nb::arg("label"), nb::arg("type"))
        .def_rw("label", &ygg::ColumnLayout::label)
        .def_rw("type", &ygg::ColumnLayout::type)
        .def(nb::self == nb::self);  // NOLINT(misc-redundant-expression)

    auto relation = nb::class_<Relation>(m,
                                         "Relation",
                                         "Mutable builder of fixed-arity tuples with unique column labels. Its rows are read "
                                         "through a repository: RelationRepository.view(relation).")
                        .def(nb::init<>())
                        .def(nb::init<ColumnIndices>(), nb::arg("columns"))
                        .def(
                            "__init__",
                            [](Relation* self, const Layouts& columns) { new (self) Relation(std::span<const ygg::ColumnLayout>(columns)); },
                            nb::arg("columns"))
                        .def(
                            "__init__",
                            [](Relation* self, const urd::Store<urd::sequence<Column>>& labels) { new (self) Relation(std::span<const ygg::ColumnLayout>(untyped(labels))); },
                            nb::arg("labels"),
                            "Columns of these labels, each of type UINT32.");
    bind_relation_shape(relation);
    relation.def("clear", &Relation::clear)
        .def(
            "insert",
            [](Relation& relation, nb::typed<nb::sequence, nb::object> row)
            {
                if (nb::len(row) != relation.columns().size())
                    throw std::invalid_argument("Relation: row arity does not match schema.");
                auto values = urd::Store<urd::sequence<ygg::Value>>(relation.columns().size());
                const auto columns = relation.columns();
                for (size_t i = 0; i < columns.size(); ++i)
                    values[i] = ygg::encode(cast_value(row[i], columns[i].type));
                return relation.insert(std::span<const ygg::Value>(values));
            },
            nb::arg("row"));

    auto borrowed = nb::class_<BuilderRelationView>(m,
                                                    "BorrowedRelation",
                                                    "Read-only view of a relation builder owned elsewhere (e.g. an evaluator result), read "
                                                    "through its repository. Keeps its owner alive; the owner's next mutation invalidates "
                                                    "the view and its rows.");
    bind_relation_reads(borrowed);

    urd::python::bind_index<urd::index<RelationTag>>(m, "RelationIndex");

    auto interned = nb::class_<RelationView>(m,
                                             "RelationView",
                                             "Read-only canonical relation. Keeps its repository alive; clearing the "
                                             "repository invalidates the view, its rows, and its column labels.")
                        .def("index", &RelationView::index);
    bind_relation_reads(interned);
    urd::python::add_comparison(interned);
    urd::python::add_hash(interned);

    nb::class_<RelationRepository>(m, "RelationRepository", "Canonical relation storage; clear invalidates all borrowed views.")
        .def(nb::init<>())
        .def("__len__", [](const RelationRepository& repository) { return repository.symbols().template size<RelationTag>(); })
        .def("clear", &RelationRepository::clear)
        .def("insert", [](RelationRepository& repository, const Relation& relation) { return repository.insert(relation); }, nb::arg("relation"))
        .def(
            "view",
            [](const RelationRepository& repository, const Relation& relation) { return repository.view(relation); },
            nb::arg("relation"),
            nb::keep_alive<0, 1>(),
            nb::keep_alive<0, 2>(),
            "The relation builder read through this repository.")
        .def(
            "get_relation_builder",
            [](const RelationRepository& repository, ColumnIndices columns) { return repository.get_relation_builder<ygg::Schema>(columns); },
            nb::arg("columns"),
            nb::keep_alive<0, 1>(),
            "A relation builder from this repository's slots, keyed by its column widths (a builder handed back under equal "
            "widths is reused, its row storage kept); it goes back with its last owner.")
        .def(
            "get_relation_builder",
            [](const RelationRepository& repository, const Layouts& columns)
            { return repository.get_relation_builder<ygg::Schema>(std::span<const ygg::ColumnLayout>(columns)); },
            nb::arg("columns"),
            nb::keep_alive<0, 1>())
        .def(
            "copy",
            [](RelationRepository& repository, RelationView source) { return repository.copy(source); },
            nb::arg("source"))
        .def(
            "rename",
            [](RelationRepository& repository, RelationView relation, const urd::Store<urd::sequence<Column>>& labels)
            {
                auto columns = Layouts(relation.columns().begin(), relation.columns().end());
                if (labels.size() != columns.size())
                    throw std::invalid_argument("Columns: renaming must preserve arity.");
                for (size_t i = 0; i < columns.size(); ++i)
                    columns[i].label = labels[i];
                return repository.rename(relation, std::span<const ygg::ColumnLayout>(columns));
            },
            nb::arg("relation"),
            nb::arg("columns"),
            nb::keep_alive<0, 1>());

    bind_plans(m);
    bind_unary<RelationView>(m);
    bind_unary<BuilderRelationView>(m);
    bind_binary<RelationView, RelationView>(m);
    bind_binary<RelationView, BuilderRelationView>(m);
    bind_binary<BuilderRelationView, RelationView>(m);
    bind_binary<BuilderRelationView, BuilderRelationView>(m);
    bind_database_queries(m);
    bind_database_optimization(m);
    bind_database_evaluation(m);
    bind_database_distance(m);
}

}  // namespace yggdrasil
