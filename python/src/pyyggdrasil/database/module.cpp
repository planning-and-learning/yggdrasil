#include "module.hpp"

#include "yggdrasil/database/operations.hpp"
#include "yggdrasil/database/relation_pool.hpp"
#include "yggdrasil/database/relation_repository.hpp"
#include "yggdrasil/python/bindings.hpp"
#include "yggdrasil/python/owner.hpp"

#include <cstddef>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/vector.h>
#include <optional>
#include <span>
#include <vector>

namespace yggdrasil
{

void bind_database_module_definitions(nb::module_& m)
{
    using RelationTag = ygg::database::Relation<>;
    using Relation = ygg::Builder<RelationTag>;
    using RelationView = ygg::database::RelationView<>;
    using RelationPtr = ygg::UniqueObjectPoolPtr<Relation>;
    using RelationPool = ygg::database::RelationPool<>;
    using RelationRepository = ygg::database::RelationRepository<>;
    using RelationRepositoryFactory = ygg::database::RelationRepositoryFactory<>;
    using Row = std::span<const ygg::uint_t>;
    using ColumnIndices = std::span<const ygg::Index<ygg::database::Column>>;
    const auto column_indices = [](const std::vector<ygg::uint_t>& columns)
    {
        std::vector<ygg::Index<ygg::database::Column>> result;
        result.reserve(columns.size());
        for (const auto column : columns)
            result.emplace_back(column);
        return result;
    };

    nb::class_<Row>(m,
                    "RelationRow",
                    "Read-only borrowed row. Keeps its Python owner alive, but clearing the owning "
                    "relation or repository invalidates the borrowed data. Memoization resets alone do not.")
        .def("__len__", &Row::size)
        .def("__getitem__",
             [](Row row, std::ptrdiff_t index)
             {
                 if (index < 0)
                     index += static_cast<std::ptrdiff_t>(row.size());
                 if (index < 0 || static_cast<std::size_t>(index) >= row.size())
                     throw nb::index_error();
                 return row[index];
             });

    nb::class_<ColumnIndices>(m,
                              "ColumnIndices",
                              "Read-only borrowed column labels exposed as integers. Keeps its Python owner alive; "
                              "replacing a builder's schema or clearing its repository invalidates the labels.")
        .def("__len__", &ColumnIndices::size)
        .def("__getitem__",
             [](ColumnIndices columns, std::ptrdiff_t index)
             {
                 if (index < 0)
                     index += static_cast<std::ptrdiff_t>(columns.size());
                 if (index < 0 || static_cast<std::size_t>(index) >= columns.size())
                     throw nb::index_error();
                 return columns[index].get_value();
             });

    nb::class_<Relation>(m, "Relation", "Mutable builder of fixed-arity tuples with unique column labels.")
        .def(nb::new_([column_indices](const std::vector<ygg::uint_t>& columns) { return new Relation(column_indices(columns)); }),
             nb::arg("columns") = std::vector<ygg::uint_t> {})
        .def("__len__", &Relation::size)
        .def(
            "__getitem__",
            [](const Relation& relation, std::ptrdiff_t index)
            {
                if (index < 0)
                    index += static_cast<std::ptrdiff_t>(relation.size());
                if (index < 0 || static_cast<std::size_t>(index) >= relation.size())
                    throw nb::index_error();
                return relation.row(index);
            },
            nb::keep_alive<0, 1>())
        .def("arity", &Relation::arity)
        .def("empty", &Relation::empty)
        .def("at", &Relation::at, nb::arg("index"), nb::keep_alive<0, 1>())
        .def(
            "columns",
            [](const Relation& relation) { return relation.columns().span(); },
            nb::keep_alive<0, 1>())
        .def("clear", &Relation::clear)
        .def("insert", [](Relation& relation, const std::vector<ygg::uint_t>& row) { return relation.insert(row); }, nb::arg("row"));

    nb::class_<RelationPtr>(m, "RelationPtr", "Owns a pooled relation until the handle and its borrowed rows are released.")
        .def("get", &RelationPtr::get, nb::rv_policy::reference_internal);

    nb::class_<RelationPool>(m, "RelationPool", "Reuses released relations and their tuple storage.")
        .def(nb::init<>())
        .def(
            "get_or_allocate",
            [column_indices](RelationPool& pool, const std::vector<ygg::uint_t>& columns) { return pool.get_or_allocate(column_indices(columns)); },
            nb::arg("columns"),
            nb::keep_alive<0, 1>());

    ygg::bind_index<ygg::Index<RelationTag>>(m, "RelationIndex");

    auto interned = nb::class_<RelationView>(m,
                                             "RelationView",
                                             "Read-only canonical relation. Keeps its repository alive; clearing the "
                                             "repository invalidates the view, its rows, and its column labels.")
                        .def("get_index", &RelationView::get_index)
                        .def("__len__", &RelationView::size)
                        .def(
                            "__getitem__",
                            [](const RelationView& relation, std::ptrdiff_t index)
                            {
                                if (index < 0)
                                    index += static_cast<std::ptrdiff_t>(relation.size());
                                if (index < 0 || static_cast<std::size_t>(index) >= relation.size())
                                    throw nb::index_error();
                                return relation.row(index);
                            },
                            nb::keep_alive<0, 1>())
                        .def("arity", &RelationView::arity)
                        .def("empty", &RelationView::empty)
                        .def(
                            "at",
                            [](const RelationView& relation, std::size_t index)
                            {
                                if (index >= relation.size())
                                    throw nb::index_error();
                                return relation.row(index);
                            },
                            nb::arg("index"),
                            nb::keep_alive<0, 1>())
                        .def("columns", [](const RelationView& relation) { return relation.columns().span(); }, nb::keep_alive<0, 1>());
    ygg::add_comparison(interned);
    ygg::add_hash(interned);

    nb::class_<RelationRepository>(m, "RelationRepository", "Canonical relation storage; clear invalidates all borrowed views.")
        .def("__len__", &RelationRepository::size)
        .def("clear", &RelationRepository::clear)
        .def(
            "rename",
            [column_indices](RelationRepository& repository,
                             RelationView relation,
                             const std::vector<ygg::uint_t>& columns,
                             std::optional<std::size_t> schema_namespace)
            {
                const auto labels = column_indices(columns);
                return schema_namespace ? repository.rename(relation, labels, *schema_namespace) : repository.rename(relation, labels);
            },
            nb::arg("relation"),
            nb::arg("columns"),
            nb::arg("schema_namespace") = nb::none(),
            nb::keep_alive<0, 1>());

    nb::class_<RelationRepositoryFactory>(m, "RelationRepositoryFactory", "Creates repositories with distinct identities within this factory.")
        .def(nb::init<>())
        .def("create", [](RelationRepositoryFactory& factory) { return new RelationRepository(factory.create()); }, nb::rv_policy::take_ownership);

    const auto retainer = ygg::python::make_owner_retainer();
    m.def(
        "insert",
        [retainer](nb::typed<nb::handle, RelationRepository> owner, Relation& builder, std::size_t schema_namespace) -> nb::typed<nb::tuple, RelationView, bool>
        {
            auto result = ygg::database::insert(nb::cast<RelationRepository&>(owner), builder, schema_namespace);
            return nb::borrow<nb::typed<nb::tuple, RelationView, bool>>(ygg::python::cast_with_owner(result, owner, retainer));
        },
        nb::arg("repository"),
        nb::arg("builder"),
        nb::arg("schema_namespace") = 0);
    m.def(
        "copy",
        [retainer](RelationView source, nb::typed<nb::handle, RelationRepository> owner) -> nb::typed<nb::tuple, RelationView, bool>
        {
            auto result = ygg::database::copy(source, nb::cast<RelationRepository&>(owner));
            return nb::borrow<nb::typed<nb::tuple, RelationView, bool>>(ygg::python::cast_with_owner(result, owner, retainer));
        },
        nb::arg("source"),
        nb::arg("repository"));
    m.def(
        "assign",
        [](nb::typed<nb::handle, Relation> destination, RelationView source)
        {
            ygg::database::assign(nb::cast<Relation&>(destination), source);
            return destination;
        },
        nb::arg("destination"),
        nb::arg("source"));
    m.def(
        "assign",
        [](nb::typed<nb::handle, Relation> destination, const Relation& source)
        {
            ygg::database::assign(nb::cast<Relation&>(destination), source);
            return destination;
        },
        nb::arg("destination"),
        nb::arg("source"));
}

}  // namespace yggdrasil
