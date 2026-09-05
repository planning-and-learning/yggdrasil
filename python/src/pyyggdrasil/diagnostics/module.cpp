#include "module.hpp"

#include <nanobind/stl/optional.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <yggdrasil/diagnostics/diagnostic.hpp>

namespace yggdrasil
{

void bind_diagnostics_module_definitions(nb::module_& m)
{
    using namespace ygg::diagnostics;

    nb::class_<Source>(m, "Source", "Immutable UTF-8 source text with an optional filename.")
        .def(nb::init<std::string, std::string, int>(), nb::arg("text"), nb::arg("filename") = "", nb::arg("tab_width") = 4)
        .def_prop_ro("text", &Source::text)
        .def_prop_ro("filename", &Source::filename)
        .def_prop_ro("tab_width", &Source::tab_width);

    nb::class_<SourceSpan>(m, "SourceSpan", "A half-open UTF-8 byte range; equal offsets identify a point.")
        .def(nb::init<std::shared_ptr<const Source>, std::size_t, std::size_t>(), nb::arg("source"), nb::arg("begin"), nb::arg("end"))
        .def_prop_ro("source", &SourceSpan::source)
        .def_prop_ro("begin", &SourceSpan::begin, "Zero-based UTF-8 byte offset.")
        .def_prop_ro("end", &SourceSpan::end, "Exclusive UTF-8 byte offset.")
        .def_prop_ro("line", &SourceSpan::line, "One-based source line.")
        .def_prop_ro("column", &SourceSpan::column, "One-based UTF-8 byte column, before tab expansion.");

    nb::class_<DiagnosticNote>(m, "DiagnosticNote", "A related message with an optional source location.")
        .def(nb::init<std::string, std::optional<SourceSpan>>(), nb::arg("message"), nb::arg("location") = nb::none())
        .def_ro("message", &DiagnosticNote::message)
        .def_ro("location", &DiagnosticNote::location);

    nb::class_<Diagnostic>(m, "Diagnostic", "A diagnostic message with an optional source location and related notes.")
        .def(nb::init<std::string, std::optional<SourceSpan>, std::vector<DiagnosticNote>>(),
             nb::arg("message"),
             nb::arg("location") = nb::none(),
             nb::arg("notes") = std::vector<DiagnosticNote> {})
        .def_ro("message", &Diagnostic::message)
        .def_ro("location", &Diagnostic::location)
        .def_ro("notes", &Diagnostic::notes)
        .def("__str__", &format_diagnostic);

    m.def("format_diagnostic", &format_diagnostic, nb::arg("diagnostic"), "Render a diagnostic and its related notes with source locations.");
}

}  // namespace yggdrasil
