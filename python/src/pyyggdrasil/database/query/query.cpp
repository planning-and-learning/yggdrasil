#include "bindings.hpp"
#include "yggdrasil/python/type_casters.hpp"

namespace yggdrasil::database_python
{
namespace
{
template<typename... Tags>
void bind_query_data_constructors(nb::class_<urd::data<ygg::database::Query<>>>& cls, boost::mp11::mp_list<Tags...>)
{
    using namespace nb::literals;
    (cls.def(nb::init<ConcreteQueryView<Tags>>(), "query"_a), ...);
}
}  // namespace

void bind_query(nb::module_& m, QueryRepositoryBinding& repository)
{
    urd::python::bind_index<urd::index<ygg::database::Query<>>>(m, "QueryIndex");

    {
        using V = urd::data<ygg::database::Query<>>;
        auto cls = urd::python::bind_data<V>(m, "QueryData");
        bind_query_data_constructors(cls, ygg::database::QueryConstructorTags {});
        cls.def_rw("variant", &V::variant);
    }

    {
        using V = QueryView;
        auto cls = nb::class_<V>(m, "Query", "Immutable interned query. Keeps its repository alive.");
        cls.def("get_index", &V::index).def("get_variant", &V::get_variant, nb::keep_alive<0, 1>()).def("columns", &V::columns, nb::keep_alive<0, 1>());
        urd::python::add_comparison(cls);
        urd::python::add_hash(cls);
    }

    ygg::bind_insert<ygg::database::Query<>>(repository);
}

}  // namespace yggdrasil::database_python

namespace yggdrasil
{
void bind_database_queries(nb::module_& m)
{
    nb::class_<ygg::database::QueryRepositoryFactory>(m, "QueryRepositoryFactory", "Creates query repositories with distinct identities within this factory.")
        .def(nb::init<>())
        .def(
            "create",
            [](ygg::database::QueryRepositoryFactory& factory) { return new ygg::database::QueryRepository(factory.create()); },
            nb::rv_policy::take_ownership);

    auto repository = database_python::QueryRepositoryBinding(m, "QueryRepository", "Interns validated relational expressions without reading data.");
    repository.def("__len__", [](const ygg::database::QueryRepository& self) { return self.size(); });
    database_python::bind_query_input(m, repository);
    database_python::bind_query_empty(m, repository);
    database_python::bind_query_join(m, repository);
    database_python::bind_query_project(m, repository);
    database_python::bind_query_rename(m, repository);
    database_python::bind_query_select_equal(m, repository);
    database_python::bind_query_select_value(m, repository);
    database_python::bind_query_union(m, repository);
    database_python::bind_query_difference(m, repository);
    database_python::bind_query_distance(m, repository);
    database_python::bind_query_generic_join(m, repository);
    database_python::bind_query_semijoin(m, repository);
    database_python::bind_query_antijoin(m, repository);
    database_python::bind_query(m, repository);
}
}  // namespace yggdrasil
