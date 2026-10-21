#include "bindings.hpp"

namespace yggdrasil::database_python
{
void bind_query_rename(nb::module_& m, QueryRepositoryBinding& repository)
{
    using namespace nb::literals;
    ygg::bind_index<ygg::Index<ygg::database::Query<ygg::database::QueryRenameTag>>>(m, "QueryRenameIndex");

    {
        using V = QueryData<ygg::database::QueryRenameTag>;
        ygg::bind_data<V>(m, "QueryRenameData")
            .def(nb::init<QueryView, std::vector<Column>>(), "arg"_a, "labels"_a)
            .def_rw("arg", &V::arg)
            .def_rw("labels", &V::labels);
    }

    {
        using V = ConcreteQueryView<ygg::database::QueryRenameTag>;
        auto cls = nb::class_<V>(m, "QueryRename");
        cls.def("get_index", &V::get_index)
            .def("columns", &V::columns, nb::keep_alive<0, 1>())
            .def("get_arg", &V::get_arg, nb::keep_alive<0, 1>());
        ygg::add_comparison(cls);
        ygg::add_hash(cls);
    }

    ygg::bind_insert<ygg::database::Query<ygg::database::QueryRenameTag>>(repository);
}

}  // namespace yggdrasil::database_python
