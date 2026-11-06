#include "bindings.hpp"
#include "yggdrasil/python/type_casters.hpp"

namespace yggdrasil::database_python
{
void bind_query_input(nb::module_& m, QueryRepositoryBinding& repository)
{
    using namespace nb::literals;
    urd::python::bind_index<urd::index<ygg::database::Query<ygg::database::QueryInputTag>>>(m, "QueryInputIndex");

    {
        using V = QueryData<ygg::database::QueryInputTag>;
        urd::python::bind_data<V>(m, "QueryInputData").def(nb::init<size_t, ColumnIndices>(), "slot"_a, "columns"_a).def_rw("input_slot", &V::input_slot);
    }

    {
        using V = ConcreteQueryView<ygg::database::QueryInputTag>;
        auto cls = nb::class_<V>(m, "QueryInput");
        cls.def("get_index", &V::index).def("columns", &V::columns, nb::keep_alive<0, 1>()).def("get_input_slot", &V::get_input_slot);
        urd::python::add_comparison(cls);
        urd::python::add_hash(cls);
    }

    ygg::bind_insert<ygg::database::Query<ygg::database::QueryInputTag>>(repository);
}

}  // namespace yggdrasil::database_python
