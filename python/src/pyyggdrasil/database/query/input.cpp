#include "bindings.hpp"

namespace yggdrasil::database_python
{
void bind_query_input(nb::module_& m, QueryRepositoryBinding& repository)
{
    using namespace nb::literals;
    ygg::bind_index<ygg::Index<ygg::database::Query<Values, ygg::database::QueryInputTag>>>(m, "QueryInputIndex");

    {
        using V = QueryData<ygg::database::QueryInputTag>;
        ygg::bind_data<V>(m, "QueryInputData")
            .def(nb::init<size_t, ColumnIndices>(), "slot"_a, "columns"_a)
            .def_rw("input_slot", &V::input_slot);
    }

    {
        using V = ConcreteQueryView<ygg::database::QueryInputTag>;
        auto cls = nb::class_<V>(m, "QueryInput");
        cls.def("get_index", &V::get_index)
            .def("columns", &V::columns, nb::keep_alive<0, 1>())
            .def("get_input_slot", &V::get_input_slot);
        ygg::add_comparison(cls);
        ygg::add_hash(cls);
    }

    ygg::bind_insert<ygg::database::Query<Values, ygg::database::QueryInputTag>>(repository);
}

}  // namespace yggdrasil::database_python
