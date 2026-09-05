/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_DIAGNOSTICS_DIAGNOSTIC_HPP_
#define YGG_DIAGNOSTICS_DIAGNOSTIC_HPP_

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ygg::diagnostics
{

/// Immutable source snapshot shared by diagnostics, independent of a parser's lifetime.
class Source
{
public:
    explicit Source(std::string text, std::string filename = "", int tab_width = 4) :
        m_text(std::move(text)),
        m_filename(std::move(filename)),
        m_tab_width(tab_width)
    {
        if (tab_width < 1)
            throw std::invalid_argument("tab_width must be at least 1");
    }

    const std::string& text() const noexcept { return m_text; }
    const std::string& filename() const noexcept { return m_filename; }
    int tab_width() const noexcept { return m_tab_width; }

private:
    const std::string m_text;
    const std::string m_filename;
    const int m_tab_width;
};

/// Zero-based byte range [begin, end); begin == end denotes a point, including EOF.
class SourceSpan
{
public:
    SourceSpan(std::shared_ptr<const Source> source, std::size_t begin, std::size_t end) : m_source(std::move(source)), m_begin(begin), m_end(end)
    {
        if (!m_source || begin > end || end > m_source->text().size())
            throw std::invalid_argument("source span must satisfy 0 <= begin <= end <= source size");
    }

    const std::shared_ptr<const Source>& source() const noexcept { return m_source; }
    std::size_t begin() const noexcept { return m_begin; }
    std::size_t end() const noexcept { return m_end; }

    /// One-based line, recognizing LF, CRLF, and CR line endings.
    std::size_t line() const noexcept
    {
        const auto& text = m_source->text();
        auto result = std::size_t { 1 };
        for (std::size_t i = 0; i < m_begin; ++i)
            if (text[i] == '\r' || (text[i] == '\n' && (i == 0 || text[i - 1] != '\r')))
                ++result;
        return result;
    }

    /// One-based byte column, not a Unicode display column. Tabs count as one byte.
    std::size_t column() const noexcept
    {
        if (m_begin == 0)
            return 1;
        const auto previous = m_source->text().find_last_of("\r\n", m_begin - 1);
        return previous == std::string::npos ? m_begin + 1 : m_begin - previous;
    }

private:
    std::shared_ptr<const Source> m_source;
    std::size_t m_begin;
    std::size_t m_end;
};

struct DiagnosticNote
{
    std::string message;
    std::optional<SourceSpan> location = std::nullopt;
};

struct Diagnostic
{
    std::string message;
    std::optional<SourceSpan> location = std::nullopt;
    std::vector<DiagnosticNote> notes = {};
};

namespace detail
{

inline void render_location(std::ostream& out, const std::string& message, const std::optional<SourceSpan>& location)
{
    if (!location)
    {
        out << message << '\n';
        return;
    }

    const auto& span = *location;
    const auto& source = *span.source();
    const auto& text = source.text();
    const auto line_start = span.begin() - (span.column() - 1);
    const auto line_end = std::min(text.find_first_of("\r\n", line_start), text.size());

    out << "In ";
    if (!source.filename().empty())
        out << "file " << source.filename() << ", ";
    out << "line " << span.line() << ":\n" << message << '\n';
    for (auto i = line_start; i < line_end; ++i)
        if (text[i] == '\t')
            out << std::string(source.tab_width(), ' ');
        else
            out << text[i];
    out << '\n';

    // ponytail: byte-based indicators (with expanded tabs); use display-width handling if terminals need Unicode alignment.
    const auto indicator = [&](std::size_t begin, std::size_t end, char marker)
    {
        for (auto i = begin; i < end; ++i)
            out << std::string(text[i] == '\t' ? source.tab_width() : 1, marker);
    };
    const auto point = span.begin() == span.end();
    indicator(line_start, span.begin(), point ? '_' : ' ');
    if (point)
        out << "^_\n";
    else
    {
        indicator(span.begin(), std::min(span.end(), line_end), '~');
        out << " <<-- Here\n";
    }
}

}  // namespace detail

/// Render the first line of each source range; complete ranges remain available in the diagnostic.
inline std::string format_diagnostic(const Diagnostic& diagnostic)
{
    auto out = std::ostringstream {};
    detail::render_location(out, diagnostic.message, diagnostic.location);
    for (const auto& note : diagnostic.notes)
        detail::render_location(out, "note: " + note.message, note.location);
    return out.str();
}

}  // namespace ygg::diagnostics

#endif
