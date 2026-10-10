#include "bindings.hpp"

namespace yggdrasil::database_python
{
using namespace nb::literals;

void bind_query_distance(nb::module_& m, QueryRepositoryBinding& repository)
{
    ygg::bind_index<ygg::Index<db::Query<Values, db::QueryDistanceTag>>>(m, "QueryDistanceIndex");

    {
        using V = QueryData<db::QueryDistanceTag>;
        ygg::bind_data<V>(m, "QueryDistanceData")
            .def(nb::init<QueryView, QueryView, QueryView, Column>(), "sources"_a, "edges"_a, "targets"_a, "distance_column"_a)
            .def_rw("sources", &V::sources)
            .def_rw("edges", &V::edges)
            .def_rw("targets", &V::targets)
            .def_rw("distance_column", &V::distance_column);
    }

    {
        using V = ConcreteQueryView<db::QueryDistanceTag>;
        auto cls = nb::class_<V>(m, "QueryDistance");
        cls.def("get_index", &V::get_index)
            .def("columns", &V::columns, nb::keep_alive<0, 1>())
            .def("get_sources", &V::get_sources, nb::keep_alive<0, 1>())
            .def("get_edges", &V::get_edges, nb::keep_alive<0, 1>())
            .def("get_targets", &V::get_targets, nb::keep_alive<0, 1>())
            .def("get_distance_column", &V::get_distance_column);
        ygg::add_comparison(cls);
        ygg::add_hash(cls);
    }

    ygg::bind_insert<db::Query<Values, db::QueryDistanceTag>>(repository);
}

}  // namespace yggdrasil::database_python
