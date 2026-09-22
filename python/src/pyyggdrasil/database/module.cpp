#include "module.hpp"

#include <cstddef>
#include <nanobind/stl/vector.h>
#include <span>
#include <vector>
#include <yggdrasil/database/relation_pool.hpp>

namespace yggdrasil
{

void bind_database_module_definitions(nb::module_& m)
{
    using Relation = ygg::database::Relation<>;
    using RelationPtr = ygg::UniqueObjectPoolPtr<Relation>;
    using RelationPool = ygg::database::RelationPool<>;
    using Row = std::span<const ygg::uint_t>;

    nb::class_<Row>(m, "RelationRow", "Read-only row that keeps its relation alive.")
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

    nb::class_<Relation>(m, "Relation", "Set of fixed-arity tuples with unique column labels.")
        .def(nb::init<const std::vector<ygg::database::Column>&>(), nb::arg("columns") = std::vector<ygg::database::Column> {})
        .def("__len__", &Relation::size)
        .def(
            "__getitem__",
            [](const Relation& relation, std::ptrdiff_t index)
            {
                if (index < 0)
                    index += static_cast<std::ptrdiff_t>(relation.size());
                if (index < 0 || static_cast<std::size_t>(index) >= relation.size())
                    throw nb::index_error();
                return relation[index];
            },
            nb::keep_alive<0, 1>())
        .def("arity", &Relation::arity)
        .def("empty", &Relation::empty)
        .def("at", &Relation::at, nb::arg("index"), nb::keep_alive<0, 1>())
        .def("insert", [](Relation& relation, const std::vector<ygg::uint_t>& row) { return relation.insert(row); }, nb::arg("row"));

    nb::class_<RelationPtr>(m, "RelationPtr", "Owns a pooled relation until the handle and its borrowed rows are released.")
        .def("get", &RelationPtr::get, nb::rv_policy::reference_internal);

    nb::class_<RelationPool>(m, "RelationPool", "Reuses released relations and their tuple storage.")
        .def(nb::init<>())
        .def(
            "get_or_allocate",
            [](RelationPool& pool, const std::vector<ygg::database::Column>& columns) { return pool.get_or_allocate(columns); },
            nb::arg("columns"),
            nb::keep_alive<0, 1>());
}

}  // namespace yggdrasil
