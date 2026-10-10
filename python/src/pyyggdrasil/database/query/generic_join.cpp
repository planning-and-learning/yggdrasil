#include "bindings.hpp"

namespace yggdrasil::database_python
{
using namespace nb::literals;

void bind_query_generic_join(nb::module_& m, QueryRepositoryBinding& repository)
{
    ygg::bind_index<ygg::Index<db::Query<Values, db::QueryGenericJoinTag>>>(m, "QueryGenericJoinIndex");

    {
        using V = QueryData<db::QueryGenericJoinTag>;
        ygg::bind_data<V>(m, "QueryGenericJoinData")
            .def(nb::init<std::vector<QueryView>, std::vector<Column>, std::vector<Column>>(),
                 "inputs"_a,
                 "variable_order"_a,
                 "output_order"_a = std::vector<Column> {})
            .def_rw("inputs", &V::inputs)
            .def_rw("variable_order", &V::variable_order)
            .def_rw("output_order", &V::output_order);
    }

    {
        using V = ConcreteQueryView<db::QueryGenericJoinTag>;
        auto cls = nb::class_<V>(m, "QueryGenericJoin");
        cls.def("get_index", &V::get_index)
            .def("columns", &V::columns, nb::keep_alive<0, 1>());
        ygg::add_comparison(cls);
        ygg::add_hash(cls);
    }

    ygg::bind_insert<db::Query<Values, db::QueryGenericJoinTag>>(repository);
}

}  // namespace yggdrasil::database_python
