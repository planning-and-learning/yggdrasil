/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DIAGNOSTICS_ERRORS_HPP_
#define YGG_DIAGNOSTICS_ERRORS_HPP_

#include "yggdrasil/diagnostics/diagnostic.hpp"

#include <stdexcept>
#include <string>
#include <utility>

// Default visibility: the errors are thrown in one library and caught (or translated to Python) in another.
#if defined(__clang__) || defined(__GNUC__)
#define YGG_DIAGNOSTICS_ERROR_API __attribute__((visibility("default")))
#else
#define YGG_DIAGNOSTICS_ERROR_API
#endif

namespace ygg::diagnostics
{

/// An error in what a parser read: its message, and the diagnostic that locates it (set once the source is known);
/// what() is the diagnostic formatted.
class YGG_DIAGNOSTICS_ERROR_API SemanticError : public std::runtime_error
{
public:
    explicit SemanticError(std::string message) : std::runtime_error(message), m_diagnostic { std::move(message) }, m_display_message(m_diagnostic.message) {}

    [[nodiscard]] const char* what() const noexcept override { return m_display_message.c_str(); }
    [[nodiscard]] const std::string& message() const noexcept { return m_diagnostic.message; }
    [[nodiscard]] const Diagnostic& diagnostic() const noexcept { return m_diagnostic; }

    void set_diagnostic(Diagnostic diagnostic)
    {
        m_diagnostic = std::move(diagnostic);
        m_display_message = format_diagnostic(m_diagnostic);
    }

private:
    Diagnostic m_diagnostic;
    std::string m_display_message;
};

/// Input a parser could not read.
class YGG_DIAGNOSTICS_ERROR_API ParseError : public SemanticError
{
public:
    explicit ParseError(std::string message) : SemanticError(std::move(message)) {}
};

}  // namespace ygg::diagnostics

#undef YGG_DIAGNOSTICS_ERROR_API

#endif
