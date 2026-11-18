#include "module.hpp"

#include "pyyggdrasil/module.hpp"
#include "yggdrasil/formalism/formatter.hpp"
#include "yggdrasil/formalism/free_relations.hpp"
#include "yggdrasil/formalism/interning.hpp"
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
        .def("erase", &Relation::template erase<std::span<const ygg::Value>>, nb::arg("repository"), nb::arg("values"))
        .def("clear", &Relation::clear);
    // The views of a builder's children in each form: built (<Name>Builder), a record not interned yet (<Name>Borrowed).
    urd::python::bind_view<urd::borrowed_view<ygg::Schema, RelationRepository, urd::builder<ygg::Schema, RelationRepository>>>(m);
    urd::python::bind_view<urd::borrowed_view<ygg::Schema, RelationRepository>>(m);
    urd::python::bind_view<urd::borrowed_view<ygg::Columns, RelationRepository, urd::builder<ygg::Columns, RelationRepository>>>(m);
    urd::python::bind_view<urd::borrowed_view<ygg::Columns, RelationRepository>>(m);
    urd::python::bind_view<ygg::RowBuilderView<ygg::Schema, RelationRepository>>(m);
    urd::python::bind_view<urd::borrowed_view<ygg::Row<ygg::Schema>, RelationRepository>>(m);
    // A relation builder read through its repository (RelationRepository.view(builder)): RelationSchemaBuilder.
    urd::python::bind_view<RelationBuilderView>(m);

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
        .def(
            "insert",
            [](RelationRepository& repository, urd::data<RelationTag>& relation) { return ygg::formalism::insert(repository, relation); },
            nb::arg("relation"),
            "Interns a relation record, canonicalized first (its rows in tuple order, unique).")
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
