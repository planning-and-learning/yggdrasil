#include "module.hpp"

#include "yggdrasil/diagnostics/diagnostic.hpp"

#include <nanobind/stl/optional.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

namespace yggdrasil
{

void bind_diagnostics_module_definitions(nb::module_& m)
{
    nb::class_<ygg::diagnostics::Source>(m, "Source", "Immutable UTF-8 source text with an optional filename.")
        .def(nb::init<std::string, std::string, int>(), nb::arg("text"), nb::arg("filename") = "", nb::arg("tab_width") = 4)
        .def_prop_ro("text", &ygg::diagnostics::Source::text)
        .def_prop_ro("filename", &ygg::diagnostics::Source::filename)
        .def_prop_ro("tab_width", &ygg::diagnostics::Source::tab_width);

    nb::class_<ygg::diagnostics::SourceSpan>(m, "SourceSpan", "A half-open UTF-8 byte range; equal offsets identify a point.")
        .def(nb::init<std::shared_ptr<const ygg::diagnostics::Source>, std::size_t, std::size_t>(), nb::arg("source"), nb::arg("begin"), nb::arg("end"))
        .def_prop_ro("source", &ygg::diagnostics::SourceSpan::source)
        .def_prop_ro("begin", &ygg::diagnostics::SourceSpan::begin, "Zero-based UTF-8 byte offset.")
        .def_prop_ro("end", &ygg::diagnostics::SourceSpan::end, "Exclusive UTF-8 byte offset.")
        .def_prop_ro("line", &ygg::diagnostics::SourceSpan::line, "One-based source line.")
        .def_prop_ro("column", &ygg::diagnostics::SourceSpan::column, "One-based UTF-8 byte column, before tab expansion.");

    nb::class_<ygg::diagnostics::DiagnosticNote>(m, "DiagnosticNote", "A related message with an optional source location.")
        .def(nb::init<std::string, std::optional<ygg::diagnostics::SourceSpan>>(), nb::arg("message"), nb::arg("location") = nb::none())
        .def_ro("message", &ygg::diagnostics::DiagnosticNote::message)
        .def_ro("location", &ygg::diagnostics::DiagnosticNote::location);

    nb::class_<ygg::diagnostics::Diagnostic>(m, "Diagnostic", "A diagnostic message with an optional source location and related notes.")
        .def(nb::init<std::string, std::optional<ygg::diagnostics::SourceSpan>, std::vector<ygg::diagnostics::DiagnosticNote>>(),
             nb::arg("message"),
             nb::arg("location") = nb::none(),
             nb::arg("notes") = std::vector<ygg::diagnostics::DiagnosticNote> {})
        .def_ro("message", &ygg::diagnostics::Diagnostic::message)
        .def_ro("location", &ygg::diagnostics::Diagnostic::location)
        .def_ro("notes", &ygg::diagnostics::Diagnostic::notes)
        .def("__str__", &ygg::diagnostics::format_diagnostic);

    m.def("format_diagnostic", &ygg::diagnostics::format_diagnostic, nb::arg("diagnostic"), "Render a diagnostic and its related notes with source locations.");
}

}  // namespace yggdrasil
