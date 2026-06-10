#include "module.hpp"

#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <yggdrasil/execution/onetbb.hpp>

#include <string>

namespace yggdrasil {

void bind_execution_module_definitions(nb::module_ &m) {
  nb::class_<ygg::ExecutionContext>(
      m, "ExecutionContext",
      "Limits oneTBB execution to a fixed number of worker threads.")
      .def(nb::new_([](std::size_t num_threads) {
             return ygg::ExecutionContext::create(num_threads);
           }),
           nb::arg("num_threads"),
           "Create an execution context with num_threads worker threads.")
      .def_prop_ro("num_threads", &ygg::ExecutionContext::get_num_threads,
                   "Configured worker thread count.")
      .def_static("max_num_threads",
                  &ygg::ExecutionContext::get_max_num_threads,
                  "Return the maximum thread count accepted by this process.")
      .def(
          "__enter__",
          [](ygg::ExecutionContext &self) -> ygg::ExecutionContext & {
            return self;
          },
          nb::rv_policy::reference_internal,
          "Return this execution context for use in with statements.")
      .def(
          "__exit__",
          [](ygg::ExecutionContext &, nb::object, nb::object, nb::object) {
            return false;
          },
          nb::arg("exc_type").none(), nb::arg("exc_value").none(),
          nb::arg("traceback").none(),
          "Release the with-statement body without suppressing exceptions.")
      .def("__repr__", [](const ygg::ExecutionContext &self) {
        return "ExecutionContext(num_threads=" +
               std::to_string(self.get_num_threads()) + ")";
      });
}

} // namespace yggdrasil
