#include "bindings.hpp"
#include "yggdrasil/python/type_casters.hpp"

namespace yggdrasil::database_python
{
void bind_query_antijoin(nb::module_& m, QueryRepositoryBinding& repository)
{
    using namespace nb::literals;
    urd::python::bind_index<urd::index<ygg::database::Query<ygg::database::QueryAntijoinTag>>>(m, "QueryAntijoinIndex");

    {
        using V = QueryData<ygg::database::QueryAntijoinTag>;
        urd::python::bind_data<V>(m, "QueryAntijoinData").def(nb::init<QueryView, QueryView>(), "lhs"_a, "rhs"_a).def_rw("lhs", &V::lhs).def_rw("rhs", &V::rhs);
    }

    {
        using V = ConcreteQueryView<ygg::database::QueryAntijoinTag>;
        auto cls = nb::class_<V>(m, "QueryAntijoin");
        cls.def("get_index", &V::index)
            .def("columns", &V::columns, nb::keep_alive<0, 1>())
            .def("get_lhs", &V::get_lhs, nb::keep_alive<0, 1>())
            .def("get_rhs", &V::get_rhs, nb::keep_alive<0, 1>());
        urd::python::add_comparison(cls);
        urd::python::add_hash(cls);
    }

    ygg::bind_insert<ygg::database::Query<ygg::database::QueryAntijoinTag>>(repository);
}

}  // namespace yggdrasil::database_python
