#include "module.hpp"

#include "pyyggdrasil/module.hpp"
#include "yggdrasil/formalism/free_relations.hpp"
#include "yggdrasil/formalism/relation_builder.hpp"
#include "yggdrasil/formalism/relation_view.hpp"
#include "yggdrasil/formalism/repository.hpp"
#include "yggdrasil/python/bindings.hpp"
#include "yggdrasil/python/relations.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <nanobind/operators.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/vector.h>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <urd/python/owner.hpp>
#include <urd/python/sequences/sequence.hpp>
#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>
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
    ygg::RowPool::row_type row;
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
/// A row record as Python builds it: its pool and its values, encoded with the pool's types when inserted.
struct RowData
{
    urd::index<ygg::Schema> pool;
    nb::list values;
};
using ColumnIndices = std::span<const ygg::ColumnLayout>;
using Layouts = urd::Store<urd::sequence<ygg::ColumnLayout>>;

/// Columns of these labels, each of type uint_t.
Layouts untyped(const urd::Store<urd::sequence<Column>>& labels)
{
    auto result = Layouts();
    for (const auto label : labels)
        result.push_back(ygg::ColumnLayout { label, ygg::type_id<ygg::uint_t> });
    return result;
}

}  // namespace

void bind_formalism_module_definitions(nb::module_& m)
{
    using RelationTag = ygg::Relation<ygg::Schema>;
    using RelationView = ygg::FreeRelationView;
    using BuilderRelationView = ygg::FreeBuilderRelationView;
    using RelationRepository = ygg::FreeRelationRepository;
    urd::python::bind_index<Column>(m, "ColumnIndex");

    nb::enum_<ygg::TypeId>(m, "ColumnType")
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
        .def(nb::init<Column, ygg::TypeId>(), nb::arg("label"), nb::arg("type"))
        .def_rw("label", &ygg::ColumnLayout::label)
        .def_rw("type", &ygg::ColumnLayout::type)
        .def(nb::self == nb::self);  // NOLINT(misc-redundant-expression)

    auto relation_class = nb::class_<Relation>(m,
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
                                  [](Relation* self, const urd::Store<urd::sequence<Column>>& labels)
                                  { new (self) Relation(std::span<const ygg::ColumnLayout>(untyped(labels))); },
                                  nb::arg("labels"),
                                  "Columns of these labels, each of type UINT32.");
    bind_relation_shape(relation_class);
    relation_class.def("clear", &Relation::clear)
        .def(
            "insert",
            [](Relation& relation, const nb::typed<nb::sequence, nb::object>& row)
            {
                if (nb::len(row) != relation.columns().size())
                    throw std::invalid_argument("Relation: row arity does not match schema.");
                auto values = urd::Store<urd::sequence<ygg::Value>>(relation.columns().size());
                const auto columns = relation.columns();
                for (size_t i = 0; i < columns.size(); ++i)
                    values[i] = ygg::encode(ygg::cast_value(row[i], columns[i].type));
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
    urd::python::bind_index<urd::index<ygg::Schema>>(m, "SchemaIndex");
    urd::python::bind_index<urd::index<ygg::Columns>>(m, "ColumnsIndex");
    urd::python::bind_index<urd::index<ygg::Row<ygg::Schema>>>(m, "RowIndex");

    nb::class_<RowData>(m, "RowData", "A row record: its pool (a SchemaIndex) and its values. RelationRepository.insert gives its id.")
        .def(nb::init<urd::index<ygg::Schema>, nb::list>(), nb::arg("pool"), nb::arg("values"))
        .def_rw("pool", &RowData::pool)
        .def_rw("values", &RowData::values);
    nb::class_<urd::data<ygg::Columns>>(m, "ColumnsData", "A columns record: labels with their types.")
        .def(
            "__init__",
            [](urd::data<ygg::Columns>* self, const Layouts& columns) { new (self) urd::data<ygg::Columns>(std::span<const ygg::ColumnLayout>(columns)); },
            nb::arg("columns"));
    nb::class_<urd::data<RelationTag>>(m,
                                       "RelationData",
                                       "A relation record: its pool, its columns and the ids of its rows in that pool. "
                                       "RelationRepository.insert sorts its rows and interns it.")
        .def(
            "__init__",
            [](urd::data<RelationTag>* self,
               urd::index<ygg::Schema> pool,
               urd::index<ygg::Columns> columns,
               const std::vector<urd::index<ygg::Row<ygg::Schema>>>& rows)
            {
                new (self) urd::data<RelationTag>();
                self->pool = pool;
                self->columns = columns;
                self->rows.set(rows.begin(), rows.end());
            },
            nb::arg("pool"),
            nb::arg("columns"),
            nb::arg("rows"))
        .def_rw("pool", &urd::data<RelationTag>::pool)
        .def_rw("columns", &urd::data<RelationTag>::columns)
        .def_prop_rw(
            "rows",
            [](const urd::data<RelationTag>& self) { return std::vector<urd::index<ygg::Row<ygg::Schema>>>(self.rows.begin(), self.rows.end()); },
            [](urd::data<RelationTag>& self, const std::vector<urd::index<ygg::Row<ygg::Schema>>>& rows) { self.rows.set(rows.begin(), rows.end()); });

    auto interned = nb::class_<RelationView>(m,
                                             "RelationView",
                                             "Read-only canonical relation. Keeps its repository alive; clearing the "
                                             "repository invalidates the view, its rows, and its column labels.")
                        .def("index", &RelationView::index)
                        .def("data", [](const RelationView& relation) { return relation.data(); }, "A copy of its record.");
    bind_relation_reads(interned);
    urd::python::add_comparison(interned);
    urd::python::add_hash(interned);

    nb::class_<RelationRepository>(m, "RelationRepository", "Canonical relation storage; clear invalidates all borrowed views.")
        .def(nb::init<>())
        .def("__len__", [](const RelationRepository& repository) { return repository.symbols().template size<RelationTag>(); })
        .def("clear", &RelationRepository::clear)
        .def(
            "schema",
            [](RelationRepository& repository, const std::vector<ygg::TypeId>& types) { return repository.schema(types); },
            nb::arg("types"),
            "The pool of free relations of these column types (declared here).")
        .def(
            "insert",
            [](RelationRepository& repository, const RowData& row)
            {
                const auto types = repository.relations().types(row.pool);
                if (nb::len(row.values) != types.size())
                    throw std::invalid_argument("Relation: row arity does not match schema.");
                auto record = urd::data<ygg::Row<ygg::Schema>>();
                record.relation = row.pool;
                for (size_t i = 0; i < types.size(); ++i)
                    record.values.push_back(ygg::encode(ygg::cast_value(row.values[i], types[i])));
                const auto [view, created] = repository.insert(record);
                return std::pair { view.data().row, created };
            },
            nb::arg("row"))
        .def(
            "insert",
            [](RelationRepository& repository, urd::data<ygg::Columns>& columns)
            {
                const auto [view, created] = repository.insert(columns);
                return std::pair { view.index(), created };
            },
            nb::arg("columns"))
        .def(
            "insert",
            [](RelationRepository& repository, urd::data<RelationTag>& relation) { return repository.insert(relation); },
            nb::arg("relation"))
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
            nb::keep_alive<0, 1>());
}

}  // namespace yggdrasil
