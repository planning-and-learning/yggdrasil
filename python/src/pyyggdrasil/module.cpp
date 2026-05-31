#include "module.hpp"

#include <nanobind/stl/shared_ptr.h>
#include <ygg/execution/onetbb.hpp>

namespace yggdrasil
{

void bind_module_definitions(nb::module_& m)
{
    nb::class_<ygg::ExecutionContext>(m, "ExecutionContext")
        .def(nb::new_([](std::size_t num_threads) { return ygg::ExecutionContext::create(num_threads); }), nb::arg("num_threads"))
        .def_prop_ro("num_threads", &ygg::ExecutionContext::get_num_threads);
}

}
