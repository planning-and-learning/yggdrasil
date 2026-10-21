#include "bindings.hpp"

namespace yggdrasil::database_python
{
void bind_query_select_equal(nb::module_& m, QueryRepositoryBinding& repository)
{
    using namespace nb::literals;
    ygg::bind_index<ygg::Index<ygg::database::Query<ygg::database::QuerySelectEqualTag>>>(m, "QuerySelectEqualIndex");

    {
        using V = QueryData<ygg::database::QuerySelectEqualTag>;
        ygg::bind_data<V>(m, "QuerySelectEqualData")
            .def(nb::init<QueryView, Column, Column>(), "arg"_a, "lhs_column"_a, "rhs_column"_a)
            .def_rw("arg", &V::arg)
            .def_rw("lhs_column", &V::lhs_column)
            .def_rw("rhs_column", &V::rhs_column);
    }

    {
        using V = ConcreteQueryView<ygg::database::QuerySelectEqualTag>;
        auto cls = nb::class_<V>(m, "QuerySelectEqual");
        cls.def("get_index", &V::get_index)
            .def("columns", &V::columns, nb::keep_alive<0, 1>())
            .def("get_arg", &V::get_arg, nb::keep_alive<0, 1>())
            .def("get_lhs_column", &V::get_lhs_column)
            .def("get_rhs_column", &V::get_rhs_column);
        ygg::add_comparison(cls);
        ygg::add_hash(cls);
    }

    ygg::bind_insert<ygg::database::Query<ygg::database::QuerySelectEqualTag>>(repository);
}

}  // namespace yggdrasil::database_python
