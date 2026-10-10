#include "bindings.hpp"

namespace yggdrasil::database_python
{
using namespace nb::literals;

namespace
{
template<typename... Tags>
void bind_query_data_constructors(nb::class_<ygg::Data<db::Query<Values>>>& cls, ygg::TypeList<Tags...>)
{
    (cls.def(nb::init<ConcreteQueryView<Tags>>(), "query"_a), ...);
}
}  // namespace

void bind_query(nb::module_& m, QueryRepositoryBinding& repository)
{
    ygg::bind_index<ygg::Index<db::Query<Values>>>(m, "QueryIndex");

    {
        using V = ygg::Data<db::Query<Values>>;
        auto cls = ygg::bind_data<V>(m, "QueryData");
        bind_query_data_constructors(cls, db::QueryConstructorTags {});
        cls.def_rw("variant", &V::variant);
    }

    {
        using V = QueryView;
        auto cls = nb::class_<V>(m, "Query", "Immutable interned query. Keeps its repository alive.");
        cls.def("get_index", &V::get_index)
            .def("get_variant", &V::get_variant, nb::keep_alive<0, 1>())
            .def("columns", &V::columns, nb::keep_alive<0, 1>());
        ygg::add_comparison(cls);
        ygg::add_hash(cls);
    }

    ygg::bind_insert<db::Query<Values>>(repository);
}

}  // namespace yggdrasil::database_python

namespace yggdrasil
{
void bind_database_queries(nb::module_& m)
{
    using namespace database_python;
    nb::class_<db::QueryRepositoryFactory<Values>>(m, "QueryRepositoryFactory", "Creates query repositories with distinct identities within this factory.")
        .def(nb::init<>())
        .def("create", [](db::QueryRepositoryFactory<Values>& factory) { return new db::QueryRepository<Values>(factory.create()); }, nb::rv_policy::take_ownership);

    auto repository = QueryRepositoryBinding(m, "QueryRepository", "Interns validated relational expressions without reading data.");
    repository.def("__len__", [](const db::QueryRepository<Values>& self) { return self.size(); });
    bind_query_input(m, repository);
    bind_query_empty(m, repository);
    bind_query_join(m, repository);
    bind_query_project(m, repository);
    bind_query_rename(m, repository);
    bind_query_select_equal(m, repository);
    bind_query_select_value(m, repository);
    bind_query_union(m, repository);
    bind_query_difference(m, repository);
    bind_query_distance(m, repository);
    bind_query_generic_join(m, repository);
    bind_query(m, repository);
}
}  // namespace yggdrasil
