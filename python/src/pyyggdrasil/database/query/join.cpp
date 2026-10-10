#include "bindings.hpp"

namespace yggdrasil::database_python
{
using namespace nb::literals;

void bind_query_join(nb::module_& m, QueryRepositoryBinding& repository)
{
    ygg::bind_index<ygg::Index<db::Query<Values, db::QueryJoinTag>>>(m, "QueryJoinIndex");

    {
        using V = QueryData<db::QueryJoinTag>;
        ygg::bind_data<V>(m, "QueryJoinData")
            .def(nb::init<QueryView, QueryView>(), "lhs"_a, "rhs"_a)
            .def_rw("lhs", &V::lhs)
            .def_rw("rhs", &V::rhs);
    }

    {
        using V = ConcreteQueryView<db::QueryJoinTag>;
        auto cls = nb::class_<V>(m, "QueryJoin");
        cls.def("get_index", &V::get_index)
            .def("columns", &V::columns, nb::keep_alive<0, 1>())
            .def("get_lhs", &V::get_lhs, nb::keep_alive<0, 1>())
            .def("get_rhs", &V::get_rhs, nb::keep_alive<0, 1>());
        ygg::add_comparison(cls);
        ygg::add_hash(cls);
    }

    ygg::bind_insert<db::Query<Values, db::QueryJoinTag>>(repository);
}

}  // namespace yggdrasil::database_python
