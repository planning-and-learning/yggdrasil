#include "module.hpp"

#include <yggdrasil/python/serialization.hpp>

namespace yggdrasil
{

void bind_serialization_module_definitions(nanobind::module_& module)
{
    namespace nb = nanobind;
    using ygg::serialization::Dictionaries;

    nb::class_<Dictionaries>(module, "Dictionaries")
        .def(nb::init<>())
        .def("tables", [](const Dictionaries& self) { return ygg::python::to_python(self.tables()); },
             nb::sig("def tables(self) -> dict[str, pyyggdrasil.serialization.table.Table]"));

}

}  // namespace yggdrasil
