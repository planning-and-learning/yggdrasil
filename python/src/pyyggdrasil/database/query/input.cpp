#include "bindings.hpp"

namespace yggdrasil::database_python
{
using namespace nb::literals;

void bind_query_input(nb::module_& m, QueryRepositoryBinding& repository)
{
    ygg::bind_index<ygg::Index<db::Query<Values, db::QueryInputTag>>>(m, "QueryInputIndex");

    {
        using V = QueryData<db::QueryInputTag>;
        ygg::bind_data<V>(m, "QueryInputData")
            .def(nb::init<size_t, ColumnIndices>(), "slot"_a, "columns"_a)
            .def_rw("input_slot", &V::input_slot);
    }

    {
        using V = ConcreteQueryView<db::QueryInputTag>;
        auto cls = nb::class_<V>(m, "QueryInput");
        cls.def("get_index", &V::get_index)
            .def("columns", &V::columns, nb::keep_alive<0, 1>())
            .def("get_input_slot", &V::get_input_slot);
        ygg::add_comparison(cls);
        ygg::add_hash(cls);
    }

    ygg::bind_insert<db::Query<Values, db::QueryInputTag>>(repository);
}

}  // namespace yggdrasil::database_python
