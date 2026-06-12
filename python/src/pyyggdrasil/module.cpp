#include "module.hpp"

#include "pyyggdrasil/execution/module.hpp"

namespace yggdrasil
{

void bind_module_definitions(nb::module_& m)
{
    m.doc() = "Python bindings for Yggdrasil native utilities.";

    auto execution = m.def_submodule("execution", "Execution utilities.");
    bind_execution_module_definitions(execution);
    m.attr("execution") = execution;
}

}  // namespace yggdrasil
