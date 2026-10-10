#include "bindings.hpp"

namespace yggdrasil::database_python
{
using namespace nb::literals;

void bind_query_project(nb::module_& m, QueryRepositoryBinding& repository)
{
    ygg::bind_index<ygg::Index<db::Query<Values, db::QueryProjectTag>>>(m, "QueryProjectIndex");

    {
        using V = QueryData<db::QueryProjectTag>;
        ygg::bind_data<V>(m, "QueryProjectData")
            .def(nb::init<QueryView, std::vector<Column>>(), "arg"_a, "labels"_a)
            .def_rw("arg", &V::arg)
            .def_rw("labels", &V::labels);
    }

    {
        using V = ConcreteQueryView<db::QueryProjectTag>;
        auto cls = nb::class_<V>(m, "QueryProject");
        cls.def("get_index", &V::get_index)
            .def("columns", &V::columns, nb::keep_alive<0, 1>())
            .def("get_arg", &V::get_arg, nb::keep_alive<0, 1>());
        ygg::add_comparison(cls);
        ygg::add_hash(cls);
    }

    ygg::bind_insert<db::Query<Values, db::QueryProjectTag>>(repository);
}

}  // namespace yggdrasil::database_python
