/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_DIAGNOSTICS_X3_HPP_
#define YGG_DIAGNOSTICS_X3_HPP_

#include "yggdrasil/diagnostics/diagnostic.hpp"

#include <boost/spirit/home/x3.hpp>
#include <boost/spirit/home/x3/support/utility/error_reporting.hpp>
#include <concepts>
#include <iterator>

namespace ygg::diagnostics
{

namespace x3 = boost::spirit::x3;
using ErrorHandlerTag = x3::error_handler_tag;

/// Input iterators and stream are borrowed during parsing; make_diagnostic() returns an owned snapshot.
/// The input must remain unchanged and all positions/AST tags must belong to this handler.
template<typename It>
class ErrorHandler : public x3::error_handler<It>
{
    static_assert(std::forward_iterator<It> && std::same_as<std::iter_value_t<It>, char>, "ErrorHandler requires forward byte-character iterators");

public:
    using Base = x3::error_handler<It>;

    struct RecordedError
    {
        It position;
        std::string message;
        std::vector<DiagnosticNote> notes = {};
    };

    ErrorHandler(It first, It last, std::ostream& output, std::string file = "", int tabs = 4) :
        Base(first, last, output, file, tabs),
        m_output(output),
        m_file(std::move(file)),
        m_tabs(tabs)
    {
        if (tabs < 1)
            throw std::invalid_argument("tab_width must be at least 1");
    }

    const std::string& file() const noexcept { return m_file; }
    int tabs() const noexcept { return m_tabs; }
    void clear_error() noexcept { m_error.reset(); }
    const std::optional<RecordedError>& last_error() const noexcept { return m_error; }

    void record_error(It position, std::string message, std::vector<DiagnosticNote> notes = {})
    {
        if (!m_error)
            m_error = RecordedError { position, std::move(message), std::move(notes) };
    }

    Diagnostic diagnostic(std::string fallback, It position) const
    {
        if (m_error)
            return Diagnostic { m_error->message, source_span(m_error->position, m_error->position), m_error->notes };
        return make_diagnostic(position, std::move(fallback));
    }

    void report_error() const
    {
        if (m_error)
            m_output << format_diagnostic(diagnostic(m_error->message, m_error->position));
    }

    SourceSpan source_span(It first, It last) const
    {
        const auto& cache = this->get_position_cache();
        if (!m_source)
            m_source = std::make_shared<const Source>(std::string(cache.first(), cache.last()), m_file, m_tabs);
        return SourceSpan(m_source, std::distance(cache.first(), first), std::distance(cache.first(), last));
    }

    Diagnostic make_diagnostic(It first, It last, std::string message) const { return Diagnostic { std::move(message), source_span(first, last) }; }

    Diagnostic make_diagnostic(It position, std::string message) const { return make_diagnostic(position, position, std::move(message)); }

    Diagnostic make_diagnostic(const x3::position_tagged& node, std::string message) const
    {
        if (node.id_first < 0 || node.id_last < 0)
            return Diagnostic { std::move(message) };
        const auto range = this->position_of(node);
        return make_diagnostic(range.begin(), range.end(), std::move(message));
    }

    void operator()(It position, const std::string& message) const { m_output << format_diagnostic(make_diagnostic(position, message)); }
    void operator()(It first, It last, const std::string& message) const { m_output << format_diagnostic(make_diagnostic(first, last, message)); }
    void operator()(const x3::position_tagged& node, const std::string& message) const { m_output << format_diagnostic(make_diagnostic(node, message)); }

private:
    std::ostream& m_output;
    std::string m_file;
    int m_tabs;
    std::optional<RecordedError> m_error;
    mutable std::shared_ptr<const Source> m_source;
};

template<typename It>
std::string format_error_at(const ErrorHandler<It>& source, It position, const std::string& message)
{
    return format_diagnostic(source.make_diagnostic(position, message));
}

template<typename It>
std::string format_error_at(const ErrorHandler<It>& source, const x3::position_tagged& node, const std::string& message)
{
    if (node.id_first < 0 || node.id_last < 0)
        return message;
    return format_error_at(source, source.position_of(node).begin(), message);
}

struct ErrorHandlerBase
{
    template<typename It, typename Ast, typename Context>
    void on_success(const It& first, const It& last, Ast& ast, const Context& context)
    {
        x3::get<ErrorHandlerTag>(context).get().tag(ast, first, last);
    }

    template<typename It, typename Exception, typename Context>
    x3::error_handler_result on_error(It&, const It& last, const Exception& error, const Context& context)
    {
        auto message = "Error! Expecting: " + std::string(error.which()) + " here:";
        auto& handler = x3::get<ErrorHandlerTag>(context).get();
        auto position = error.where();
        x3::skip_over(position, last, context);
        handler.record_error(position, message);
        handler.report_error();
        return x3::error_handler_result::fail;
    }
};

/// Label a grammar expression without changing its grammar or scanning its input separately.
struct ContextDirective
{
    std::string name;

    template<typename Subject>
    auto operator[](const Subject& subject) const
    {
        return x3::as_parser(subject).on_error(*this);
    }

    template<typename It, typename Exception, typename Context>
    x3::error_handler_result operator()(It& first, const It& last, const Exception& error, const Context& parser_context) const
    {
        auto start = first;
        x3::skip_over(start, last, parser_context);
        auto position = error.where();
        x3::skip_over(position, last, parser_context);
        auto& handler = x3::get<ErrorHandlerTag>(parser_context).get();
        handler.record_error(position,
                             "Expected " + std::string(error.which()) + " while parsing " + name,
                             { DiagnosticNote { name + " starts here", handler.source_span(start, start) } });
        return x3::error_handler_result::rethrow;
    }
};

inline auto context(std::string name) { return ContextDirective { std::move(name) }; }

}  // namespace ygg::diagnostics

namespace boost::spirit::x3
{

template<typename Subject>
struct get_info<guard<Subject, ygg::diagnostics::ContextDirective>>
{
    using result_type = std::string;
    std::string operator()(const guard<Subject, ygg::diagnostics::ContextDirective>& parser) const { return parser.handler.name; }
};

}  // namespace boost::spirit::x3

#endif
