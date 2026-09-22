#include "module.hpp"

#include <boost/iterator/counting_iterator.hpp>
#include <boost/iterator/transform_iterator.hpp>
#include <cstddef>
#include <nanobind/make_iterator.h>
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
        .def(
            "__iter__",
            [](Row row) { return nb::make_iterator(nb::type<Row>(), "Iterator", row.begin(), row.end()); },
            nb::keep_alive<0, 1>())
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
            "__iter__",
            [](const Relation& relation)
            {
                const auto row_at = [&relation](std::size_t index) { return relation[index]; };
                return nb::make_iterator(nb::type<Relation>(),
                                         "Iterator",
                                         boost::make_transform_iterator(boost::counting_iterator<std::size_t>(0), row_at),
                                         boost::make_transform_iterator(boost::counting_iterator<std::size_t>(relation.size()), row_at),
                                         nb::keep_alive<0, 1>());
            },
            nb::keep_alive<0, 1>())
        .def("arity", &Relation::arity)
        .def("empty", &Relation::empty)
        .def("at", &Relation::at, nb::arg("index"), nb::keep_alive<0, 1>())
        .def(
            "insert",
            [](Relation& relation, const std::vector<ygg::uint_t>& row) { return relation.insert(std::span<const ygg::uint_t>(row)); },
            nb::arg("row"));

    nb::class_<RelationPtr>(m, "RelationPtr", "Owns a pooled relation until the handle and its borrowed rows are released.")
        .def("get", &RelationPtr::get, nb::rv_policy::reference_internal);

    nb::class_<RelationPool>(m, "RelationPool", "Reuses released relations and their tuple storage.")
        .def(nb::init<>())
        .def(
            "get_or_allocate",
            [](RelationPool& pool, const std::vector<ygg::database::Column>& columns)
            { return pool.get_or_allocate(std::span<const ygg::database::Column>(columns)); },
            nb::arg("columns"),
            nb::keep_alive<0, 1>());
}

}  // namespace yggdrasil
