#include "module.hpp"

#include "pyyggdrasil/module.hpp"
#include "yggdrasil/formalism/builder_instantiations.hpp"
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
    using RowView = ygg::RowView<ygg::Schema, RelationRepository>;
    using RelationView = ygg::FreeRelationView;
    using RelationBuilderView = ygg::FreeRelationBuilderView;
    using Relation = ygg::FreeRelationBuilder;

    urd::python::bind_index<ygg::Column>(m);
    urd::python::bind_index<ygg::formalism::Object>(m);
    nb::enum_<ygg::TypeId>(m, "ColumnType")
        .value("UINT32", ygg::type_id<std::uint32_t>)
        .value("INT32", ygg::type_id<std::int32_t>)
        .value("UINT64", ygg::type_id<std::uint64_t>)
        .value("INT64", ygg::type_id<std::int64_t>)
        .value("FLOAT32", ygg::type_id<float>)
        .value("FLOAT64", ygg::type_id<double>)
        .value("BOOL", ygg::type_id<bool>)
        .value("OBJECT", ygg::type_id<urd::index<ygg::formalism::Object>>);
    urd::python::bind_data<ygg::ColumnLayout>(m);
    urd::python::bind_index<ygg::Row<ygg::Schema>>(m);
    urd::python::bind_index<urd::key<ygg::Schema, ygg::Row<ygg::Schema>>>(m);

    // The records: <Name>Index, <Name>Data, <Name>Builder and <Name>, the view.
    urd::python::bind_index<ygg::Schema>(m);
    urd::python::bind_data<ygg::Schema>(m);
    urd::python::bind_builder<ygg::Schema, RelationRepository>(m);
    urd::python::bind_view<SchemaView>(m);
    urd::python::bind_index<ygg::Columns>(m);
    urd::python::bind_data<ygg::Columns>(m);
    urd::python::bind_builder<ygg::Columns, RelationRepository>(m);
    urd::python::bind_view<ColumnsView>(m);
    urd::python::bind_data<ygg::Row<ygg::Schema>>(m);
    urd::python::bind_builder<ygg::Row<ygg::Schema>, RelationRepository>(m);
    urd::python::bind_view<RowView>(m);
    urd::python::bind_index<RelationTag>(m);
    urd::python::bind_data<RelationTag>(m);
    // A relation builder's rows are drawn from row builders (its insert's): RowSchemaRecords, created once, reused.
    nb::class_<ygg::RowRecords<ygg::Schema, RelationRepository>>(m, "RowSchemaRecords", "The row builders a relation builder's rows are drawn from.")
        .def(nb::init<>());
    urd::python::bind_builder<RelationTag, RelationRepository>(m);
    urd::python::bind_view<RelationView>(m);

    // The views of a builder's children in each form: built (<Name>BuilderView), a record not interned yet
    // (<Name>RecordView).
    urd::python::bind_view<urd::borrowed_view<ygg::Schema, RelationRepository, urd::builder<ygg::Schema, RelationRepository>>>(m);
    urd::python::bind_view<urd::borrowed_view<ygg::Schema, RelationRepository>>(m);
    urd::python::bind_view<urd::borrowed_view<ygg::Columns, RelationRepository, urd::builder<ygg::Columns, RelationRepository>>>(m);
    urd::python::bind_view<urd::borrowed_view<ygg::Columns, RelationRepository>>(m);
    urd::python::bind_view<ygg::RowBuilderView<ygg::Schema, RelationRepository>>(m);
    urd::python::bind_view<urd::borrowed_view<ygg::Row<ygg::Schema>, RelationRepository>>(m);
    // A relation builder read through its repository (RelationRepository.view(builder)): RelationSchemaBuilderView.
    urd::python::bind_view<RelationBuilderView>(m);

    nb::class_<ygg::FreeRelationStorage>(m, "FreeRelationStorage", "The builders and scratch records free relations are interned through; create once, reuse.")
        .def(nb::init<>());

    nb::class_<RelationRepository>(m, "RelationRepository", "Canonical relation storage; clear invalidates all borrowed views.")
        .def(nb::init<>())
        .def("__len__", [](const RelationRepository& repository) { return repository.size<RelationTag>(); })
        .def("clear", &RelationRepository::clear)
        .def("insert",
             static_cast<std::pair<SchemaView, bool> (RelationRepository::*)(urd::data<ygg::Schema>&)>(&RelationRepository::insert),
             nb::arg("schema"),
             "Interns a Schema: the pool of free relations of its column types.")
        .def(
            "insert",
            [](RelationRepository& repository, const urd::builder<ygg::Row<ygg::Schema>, RelationRepository>& row, ygg::FreeRelationStorage& storage)
            { return repository.insert(row, storage); },
            nb::arg("row"),
            nb::arg("storage"),
            "Stores a row builder's row in its relation's pool where no layer holds it: its view, and whether it was stored now.")
        .def(
            "insert",
            [](RelationRepository& repository, urd::data<ygg::Row<ygg::Schema>>& row) { return repository.insert(row); },
            nb::arg("row"),
            "Stores a row record in its relation's pool where no layer holds it: its view, and whether it was stored now.")
        .def("insert",
             static_cast<std::pair<ColumnsView, bool> (RelationRepository::*)(urd::data<ygg::Columns>&)>(&RelationRepository::insert),
             nb::arg("columns"))
        .def(
            "insert",
            [](RelationRepository& repository, urd::data<RelationTag>& relation) { return repository.insert(relation); },
            nb::arg("relation"),
            "Interns a relation record, canonicalized first (its rows in tuple order, unique).")
        .def(
            "insert",
            [](RelationRepository& repository, const Relation& relation, ygg::FreeRelationStorage& storage) { return repository.insert(relation, storage); },
            nb::arg("relation"),
            nb::arg("storage"),
            "Interns a relation builder through a builder storage (FreeRelationStorage, reused across calls): its record, its "
            "rows in tuple order.")
        .def("view",
             static_cast<RelationBuilderView (RelationRepository::*)(const Relation&) const>(&RelationRepository::view),
             nb::arg("relation"),
             nb::keep_alive<0, 1>(),
             nb::keep_alive<0, 2>(),
             "The relation builder read through this repository.");
}

}  // namespace yggdrasil

// The free relations' builder storage the module hands out (FreeRelationStorage), instantiated here.
YGG_FORMALISM_BUILDER(, ygg::FreeRelationStorage, 4)
