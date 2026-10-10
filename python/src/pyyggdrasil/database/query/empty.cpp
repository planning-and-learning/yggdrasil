#include "bindings.hpp"

namespace yggdrasil::database_python
{
using namespace nb::literals;

void bind_query_empty(nb::module_& m, QueryRepositoryBinding& repository)
{
    ygg::bind_index<ygg::Index<db::Query<Values, db::QueryEmptyTag>>>(m, "QueryEmptyIndex");

    {
        using V = QueryData<db::QueryEmptyTag>;
        ygg::bind_data<V>(m, "QueryEmptyData").def(nb::init<ColumnIndices>(), "columns"_a);
    }

    {
        using V = ConcreteQueryView<db::QueryEmptyTag>;
        auto cls = nb::class_<V>(m, "QueryEmpty");
        cls.def("get_index", &V::get_index)
            .def("columns", &V::columns, nb::keep_alive<0, 1>());
        ygg::add_comparison(cls);
        ygg::add_hash(cls);
    }

    ygg::bind_insert<db::Query<Values, db::QueryEmptyTag>>(repository);
}

}  // namespace yggdrasil::database_python
