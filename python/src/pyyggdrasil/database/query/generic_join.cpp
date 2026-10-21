#include "bindings.hpp"

namespace yggdrasil::database_python
{
void bind_query_generic_join(nb::module_& m, QueryRepositoryBinding& repository)
{
    using namespace nb::literals;
    ygg::bind_index<ygg::Index<ygg::database::Query<ygg::database::QueryGenericJoinTag>>>(m, "QueryGenericJoinIndex");

    {
        using V = QueryData<ygg::database::QueryGenericJoinTag>;
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
        using V = ConcreteQueryView<ygg::database::QueryGenericJoinTag>;
        auto cls = nb::class_<V>(m, "QueryGenericJoin");
        cls.def("get_index", &V::get_index)
            .def("columns", &V::columns, nb::keep_alive<0, 1>());
        ygg::add_comparison(cls);
        ygg::add_hash(cls);
    }

    ygg::bind_insert<ygg::database::Query<ygg::database::QueryGenericJoinTag>>(repository);
}

}  // namespace yggdrasil::database_python
