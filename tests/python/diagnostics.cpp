/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "diagnostics.hpp"

#include "yggdrasil/python/diagnostics.hpp"

namespace nb = nanobind;

namespace
{

class ParserError : public std::runtime_error
{
public:
    explicit ParserError(ygg::diagnostics::Diagnostic diagnostic) :
        std::runtime_error(ygg::diagnostics::format_diagnostic(diagnostic)),
        m_diagnostic(std::move(diagnostic))
    {
    }

    [[nodiscard]] const ygg::diagnostics::Diagnostic& diagnostic() const noexcept { return m_diagnostic; }

private:
    ygg::diagnostics::Diagnostic m_diagnostic;
};

class SemanticError : public ParserError
{
public:
    using ParserError::ParserError;
};

}  // namespace

void bind_diagnostics_test(nanobind::module_& m)
{
    const auto parser_error = ygg::bind_diagnostic_exception<ParserError>(m, "ParserError", PyExc_RuntimeError);
    ygg::bind_diagnostic_exception<SemanticError>(m, "SemanticError", parser_error);

    m.def("raise_base", [] { throw ParserError(ygg::diagnostics::Diagnostic { "base failure" }); });
    m.def("raise_semantic",
          []
          {
              auto source = std::make_shared<ygg::diagnostics::Source>("first\nsecond\n", "temporary.policy");
              auto related = std::make_shared<ygg::diagnostics::Source>("related", "related.policy");
              throw SemanticError(
                  ygg::diagnostics::Diagnostic { "invalid second declaration",
                                                 ygg::diagnostics::SourceSpan(source, 6, 12),
                                                 { ygg::diagnostics::DiagnosticNote { "first declaration", ygg::diagnostics::SourceSpan(source, 0, 5) },
                                                   ygg::diagnostics::DiagnosticNote { "related declaration", ygg::diagnostics::SourceSpan(related, 0, 7) } } });
          });
    m.def("raise_unrelated", [] { throw std::runtime_error("unrelated failure"); });
    m.def("roundtrip_span", [](ygg::diagnostics::SourceSpan span) { return span; });
    m.def("roundtrip_diagnostic", [](ygg::diagnostics::Diagnostic diagnostic) { return diagnostic; });
}
