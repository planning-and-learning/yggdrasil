#include "bindings.hpp"

namespace yggdrasil::database_python
{
using namespace nb::literals;

void bind_query_select_value(nb::module_& m, QueryRepositoryBinding& repository)
{
    ygg::bind_index<ygg::Index<db::Query<Values, db::QuerySelectValueTag>>>(m, "QuerySelectValueIndex");

    {
        using V = QueryData<db::QuerySelectValueTag>;
        // Python values are cast to the given column type; the record encodes them.
        ygg::bind_data<V>(m, "QuerySelectValueData")
            .def(
                "__init__",
                [](V* self, QueryView arg, Column column, ColumnType type, nb::handle value)
                {
                    db::visit_column_type<Values>(static_cast<size_t>(type),
                                                  [&]<typename T>(std::type_identity<T>)
                                                  {
                                                      T converted;
                                                      if (!nb::try_cast(value, converted))
                                                          throw nb::type_error("Query selection: value does not match the column type.");
                                                      new (self) V(arg, column, converted);
                                                  });
                },
                "arg"_a,
                "column"_a,
                "type"_a,
                "value"_a)
            .def_rw("arg", &V::arg)
            .def_rw("column", &V::column);
    }

    {
        using V = ConcreteQueryView<db::QuerySelectValueTag>;
        auto cls = nb::class_<V>(m, "QuerySelectValue");
        cls.def("get_index", &V::get_index)
            .def("columns", &V::columns, nb::keep_alive<0, 1>())
            .def("get_arg", &V::get_arg, nb::keep_alive<0, 1>())
            .def("get_column", &V::get_column);
        ygg::add_comparison(cls);
        ygg::add_hash(cls);
    }

    ygg::bind_insert<db::Query<Values, db::QuerySelectValueTag>>(repository);
}

}  // namespace yggdrasil::database_python
