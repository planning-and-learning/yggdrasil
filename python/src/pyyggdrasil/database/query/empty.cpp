#include "bindings.hpp"

namespace yggdrasil::database_python
{
void bind_query_empty(nb::module_& m, QueryRepositoryBinding& repository)
{
    using namespace nb::literals;
    ygg::bind_index<ygg::Index<ygg::database::Query<ygg::database::QueryEmptyTag>>>(m, "QueryEmptyIndex");

    {
        using V = QueryData<ygg::database::QueryEmptyTag>;
        ygg::bind_data<V>(m, "QueryEmptyData").def(nb::init<ColumnIndices>(), "columns"_a);
    }

    {
        using V = ConcreteQueryView<ygg::database::QueryEmptyTag>;
        auto cls = nb::class_<V>(m, "QueryEmpty");
        cls.def("get_index", &V::get_index)
            .def("columns", &V::columns, nb::keep_alive<0, 1>());
        ygg::add_comparison(cls);
        ygg::add_hash(cls);
    }

    ygg::bind_insert<ygg::database::Query<ygg::database::QueryEmptyTag>>(repository);
}

}  // namespace yggdrasil::database_python
