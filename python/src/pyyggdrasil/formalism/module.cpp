#include "module.hpp"

#include "pyyggdrasil/module.hpp"
#include "yggdrasil/formalism/formatter.hpp"
#include "yggdrasil/formalism/free_relations.hpp"
#include "yggdrasil/formalism/relation_builder.hpp"
#include "yggdrasil/formalism/relation_view.hpp"
#include "yggdrasil/formalism/repository_impl.hpp"
#include "yggdrasil/python/bindings.hpp"
#include "yggdrasil/python/relations.hpp"

#include <cstddef>
#include <cstdint>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/variant.h>
#include <span>
#include <urd/python/bindings.hpp>
#include <urd/python/owner.hpp>

namespace yggdrasil
{

namespace
{
using Column = urd::index<ygg::Column>;
using Relation = ygg::FreeRelationBuilder;

/// The shape of every relation class: its arity and its columns (its rows, len(get_rows()) of a view).
template<typename R>
void bind_relation_shape(nb::class_<R>& cls)
{ cls.def("arity", &R::arity).def("columns", &R::columns); }

/// The reads of every relation view class: its shape and its rows, one by its position or all.
template<typename R>
void bind_relation_reads(nb::class_<R>& cls)
{
    bind_relation_shape(cls);
    // Each row keeps what it is read through alive (urd/python/owner.hpp), also inside the list get_rows() returns.
    cls.def("row", &R::row, nb::arg("index"));
}

/// The reads of every row class: each column's value, type and decoded value (its number, len(get_values())).
template<typename R>
void bind_row_reads(nb::class_<R>& cls)
{
    using Columns = urd::view_mixin<ygg::Row<ygg::Schema>, R>;
    cls.def("value", &R::value, nb::arg("index"))
        .def("type", &R::type, nb::arg("index"))
        .def("get", static_cast<ygg::ColumnValue (Columns::*)(std::size_t) const>(&R::get), nb::arg("index"));
}

}  // namespace

void bind_formalism_module_definitions(nb::module_& m)
{
    // A row's values as its columns store them, and back: Python composes them with RowData and Row.get_values().
    // bool before the integers: a Python bool is an int too.
    m.def("encode", nb::overload_cast<bool, ygg::TypeId>(&ygg::encode), nb::arg("value"), nb::arg("type"))
        .def("encode", nb::overload_cast<std::int64_t, ygg::TypeId>(&ygg::encode), nb::arg("value"), nb::arg("type"))
        .def("encode", nb::overload_cast<std::uint64_t, ygg::TypeId>(&ygg::encode), nb::arg("value"), nb::arg("type"))
        .def("encode", nb::overload_cast<double, ygg::TypeId>(&ygg::encode), nb::arg("value"), nb::arg("type"))
        .def("encode", nb::overload_cast<urd::index<ygg::formalism::Object>, ygg::TypeId>(&ygg::encode), nb::arg("value"), nb::arg("type"));
    m.def("decode", &ygg::decode, nb::arg("bits"), nb::arg("type"));

    using RelationTag = ygg::Relation<ygg::Schema>;
    using RelationRepository = ygg::FreeRelationRepository;
    using SchemaView = urd::index_view<ygg::Schema, RelationRepository>;
    using ColumnsView = urd::index_view<ygg::Columns, RelationRepository>;
    using StoredRow = ygg::RowView<ygg::Schema, RelationRepository>;
    using RelationView = ygg::FreeRelationView;
    using RelationBuilderView = ygg::FreeRelationBuilderView;

    urd::python::bind_index<Column>(m, "ColumnIndex");
    urd::python::bind_index<urd::index<ygg::formalism::Object>>(m, "ObjectIndex");
    nb::enum_<ygg::TypeId>(m, "ColumnType")
        .value("UINT32", ygg::type_id<std::uint32_t>)
        .value("INT32", ygg::type_id<std::int32_t>)
        .value("UINT64", ygg::type_id<std::uint64_t>)
        .value("INT64", ygg::type_id<std::int64_t>)
        .value("FLOAT32", ygg::type_id<float>)
        .value("FLOAT64", ygg::type_id<double>)
        .value("BOOL", ygg::type_id<bool>)
        .value("OBJECT", ygg::type_id<urd::index<ygg::formalism::Object>>);
    urd::python::bind_value<ygg::ColumnLayout>(m);
    urd::python::bind_index<urd::index<ygg::Row<ygg::Schema>>>(m, "RowId");
    urd::python::bind_value<ygg::RowIndex<ygg::Schema>>(m);

    // The records: <Name>Index, <Name>Data and <Name>, the view.
    urd::python::bind_data<ygg::Schema>(m);
    urd::python::bind_view<SchemaView>(m);
    urd::python::bind_data<ygg::Columns>(m);
    urd::python::bind_view<ColumnsView>(m);
    urd::python::bind_data<ygg::Row<ygg::Schema>>(m);
    urd::python::bind_view<StoredRow>(m);
    urd::python::bind_data<RelationTag>(m);
    urd::python::bind_view<RelationView>(m);

    // A relation builder mutates through its repository, which interns its rows and columns; it is read through it
    // (RelationRepository.view(builder): its view, RelationSchemaBuilder).
    nb::class_<Relation>(m, "RelationBuilder", "A relation being built: its rows in tuple order, interned in a repository.")
        .def(nb::init<>())
        .def("initialize",
             static_cast<void (Relation::*)(RelationRepository&, std::span<const ygg::ColumnLayout>)>(&Relation::initialize),
             nb::arg("repository"),
             nb::arg("columns"))
        .def("rename", &Relation::rename, nb::arg("repository"), nb::arg("columns"))
        .def("insert", &Relation::insert, nb::arg("repository"), nb::arg("values"))
        .def("erase", &Relation::erase, nb::arg("repository"), nb::arg("values"))
        .def("clear", &Relation::clear);
    auto builder_view = nb::class_<RelationBuilderView>(m,
                                                        "RelationBuilderView",
                                                        "A relation builder read through its repository. Keeps both alive; the builder's next "
                                                        "mutation invalidates the view and its rows.");
    builder_view.def("get_rows", &RelationBuilderView::get_rows);
    builder_view.def("size", &RelationBuilderView::size).def("empty", &RelationBuilderView::empty);
    builder_view.def("contains", &RelationBuilderView::template contains<std::span<const ygg::Value>>, nb::arg("values"))
        .def("find", &RelationBuilderView::template find<std::span<const ygg::Value>>, nb::arg("values"));
    bind_relation_reads(builder_view);

    nb::class_<RelationRepository>(m, "RelationRepository", "Canonical relation storage; clear invalidates all borrowed views.")
        .def(nb::init<>())
        .def("__len__", &RelationRepository::template size<RelationTag>)
        .def("clear", &RelationRepository::clear)
        .def("schema",
             nb::overload_cast<std::span<const ygg::TypeId>>(&RelationRepository::schema),
             nb::arg("types"),
             "The pool of free relations of these column types (declared here).")
        .def("insert",
             static_cast<std::pair<StoredRow, bool> (RelationRepository::*)(const urd::data<ygg::Row<ygg::Schema>>&)>(&RelationRepository::insert),
             nb::arg("row"))
        .def("insert",
             static_cast<std::pair<ColumnsView, bool> (RelationRepository::*)(urd::data<ygg::Columns>&)>(&RelationRepository::insert),
             nb::arg("columns"))
        .def("insert",
             static_cast<std::pair<RelationView, bool> (RelationRepository::*)(urd::data<RelationTag>&)>(&RelationRepository::insert),
             nb::arg("relation"))
        .def(
            "insert",
            [](RelationRepository& repository, const Relation& relation) { return ygg::intern(relation, repository); },
            nb::arg("relation"),
            "Interns a relation builder: its record, its rows in tuple order.")
        .def("view",
             static_cast<RelationBuilderView (RelationRepository::*)(const Relation&) const>(&RelationRepository::view),
             nb::arg("relation"),
             nb::keep_alive<0, 1>(),
             nb::keep_alive<0, 2>(),
             "The relation builder read through this repository.");
}

}  // namespace yggdrasil
