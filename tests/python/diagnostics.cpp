/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "yggdrasil/python/diagnostics.hpp"

namespace nb = nanobind;
using namespace ygg::diagnostics;

namespace
{

class ParserError : public std::runtime_error
{
public:
    explicit ParserError(Diagnostic diagnostic) : std::runtime_error(format_diagnostic(diagnostic)), m_diagnostic(std::move(diagnostic)) {}

    const Diagnostic& diagnostic() const noexcept { return m_diagnostic; }

private:
    Diagnostic m_diagnostic;
};

class SemanticError : public ParserError
{
public:
    using ParserError::ParserError;
};

}  // namespace

NB_MODULE(yggdrasil_diagnostics_test, m)
{
    const auto parser_error = ygg::bind_diagnostic_exception<ParserError>(m, "ParserError", PyExc_RuntimeError);
    ygg::bind_diagnostic_exception<SemanticError>(m, "SemanticError", parser_error);

    m.def("raise_base", [] { throw ParserError(Diagnostic { "base failure" }); });
    m.def("raise_semantic",
          []
          {
              auto source = std::make_shared<Source>("first\nsecond\n", "temporary.policy");
              auto related = std::make_shared<Source>("related", "related.policy");
              throw SemanticError(Diagnostic {
                  "invalid second declaration",
                  SourceSpan(source, 6, 12),
                  { DiagnosticNote { "first declaration", SourceSpan(source, 0, 5) }, DiagnosticNote { "related declaration", SourceSpan(related, 0, 7) } } });
          });
    m.def("raise_unrelated", [] { throw std::runtime_error("unrelated failure"); });
    m.def("roundtrip_span", [](SourceSpan span) { return span; });
    m.def("roundtrip_diagnostic", [](Diagnostic diagnostic) { return diagnostic; });
}
