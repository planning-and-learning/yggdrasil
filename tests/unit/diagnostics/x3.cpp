/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "yggdrasil/diagnostics/x3.hpp"

#include <boost/fusion/include/adapt_struct.hpp>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <sstream>
#include <string>

namespace ygg::tests
{

namespace
{
struct Number : boost::spirit::x3::position_tagged
{
    int value = 0;
};
}  // namespace

}  // namespace ygg::tests

BOOST_FUSION_ADAPT_STRUCT(ygg::tests::Number, value)

namespace ygg::tests
{

using Handler = diagnostics::ErrorHandler<std::string::const_iterator>;

namespace
{
struct NumberRule : diagnostics::ErrorHandlerBase
{
};
auto number() { return boost::spirit::x3::rule<NumberRule, Number> { "number" } = ('(' > boost::spirit::x3::int_) > ')'; }
}  // namespace

TEST(YggdrasilX3Diagnostics, GrammarSuccessTagsAstWithItsSourceRange)
{
    const auto source = std::string(" (42) ");
    auto first = source.cbegin();
    auto output = std::ostringstream {};
    auto handler = Handler(first, source.cend(), output, "numbers", 2);
    auto ast = Number {};
    const auto grammar = boost::spirit::x3::with<diagnostics::ErrorHandlerTag>(std::ref(handler))[number()];
    ASSERT_TRUE(boost::spirit::x3::phrase_parse(first, source.cend(), grammar, boost::spirit::x3::ascii::space, ast));
    EXPECT_EQ(first, source.cend());
    EXPECT_EQ(ast.value, 42);
    EXPECT_FALSE(handler.last_error());
    EXPECT_TRUE(output.str().empty());

    const auto diagnostic = handler.make_diagnostic(ast, "number");
    ASSERT_TRUE(diagnostic.location);
    EXPECT_EQ(diagnostic.location.value().begin(), 1);
    EXPECT_EQ(diagnostic.location.value().end(), 5);
    EXPECT_EQ(diagnostics::format_diagnostic(diagnostic), "In file numbers, line 1:\nnumber\n (42) \n ~~~~ <<-- Here\n");
    EXPECT_EQ(diagnostics::format_error_at(handler, ast, "number"), diagnostics::format_error_at(handler, source.cbegin() + 1, "number"));
    handler(ast, "number");
    EXPECT_EQ(output.str(), diagnostics::format_diagnostic(diagnostic));
}

TEST(YggdrasilX3Diagnostics, GrammarExpectationFailureRecordsAndRendersEof)
{
    const auto source = std::string("(42");
    auto first = source.cbegin();
    auto output = std::ostringstream {};
    auto handler = Handler(first, source.cend(), output);
    auto ast = Number {};
    const auto grammar = boost::spirit::x3::with<diagnostics::ErrorHandlerTag>(std::ref(handler))[number()];
    EXPECT_FALSE(boost::spirit::x3::phrase_parse(first, source.cend(), grammar, boost::spirit::x3::ascii::space, ast));
    ASSERT_TRUE(handler.last_error());
    EXPECT_EQ(handler.last_error().value().position, source.cend());
    EXPECT_NE(handler.last_error().value().message.find("Expecting:"), std::string::npos);
    EXPECT_NE(handler.last_error().value().message.find(')'), std::string::npos);
    EXPECT_EQ(output.str(), diagnostics::format_error_at(handler, source.cend(), handler.last_error().value().message));
    EXPECT_NE(output.str().find("\n(42\n___^_\n"), std::string::npos);
}

TEST(YggdrasilX3Diagnostics, PreservesFirstRecordedErrorUntilExplicitReset)
{
    const auto source = std::string("abc");
    auto output = std::ostringstream {};
    auto handler = Handler(source.cbegin(), source.cend(), output);
    handler.record_error(source.cbegin(), "first");
    handler.record_error(source.cend(), "second");
    ASSERT_TRUE(handler.last_error());
    EXPECT_EQ(handler.last_error().value().position, source.cbegin());
    EXPECT_EQ(handler.last_error().value().message, "first");
    EXPECT_TRUE(output.str().empty());
    handler.clear_error();
    EXPECT_FALSE(handler.last_error());
    handler.record_error(source.cend(), "next");
    ASSERT_TRUE(handler.last_error());
    EXPECT_EQ(handler.last_error().value().position, source.cend());
    EXPECT_EQ(handler.last_error().value().message, "next");
}

TEST(YggdrasilX3Diagnostics, UntaggedNodesHaveNoLocation)
{
    const auto source = std::string("");
    auto output = std::ostringstream {};
    auto handler = Handler(source.cbegin(), source.cend(), output);
    const auto node = boost::spirit::x3::position_tagged {};
    const auto diagnostic = handler.make_diagnostic(node, "fallback");
    EXPECT_FALSE(diagnostic.location);
    EXPECT_EQ(diagnostic.message, "fallback");
    EXPECT_EQ(diagnostics::format_error_at(handler, node, "fallback"), "fallback");
    handler(node, "fallback");
    EXPECT_EQ(output.str(), "fallback\n");
    EXPECT_EQ(diagnostics::format_error_at(handler, source.cend(), "empty"), "In line 1:\nempty\n\n^_\n");
    EXPECT_THROW((Handler(source.cbegin(), source.cend(), output, "", 0)), std::invalid_argument);
}

TEST(YggdrasilX3Diagnostics, MaterializedDiagnosticsShareSourceAndOutliveParser)
{
    const auto diagnostic = []
    {
        const auto source = std::string("é\tx");
        auto output = std::ostringstream {};
        auto handler = Handler(source.cbegin(), source.cend(), output, "utf8", 2);
        auto primary = handler.make_diagnostic(source.cbegin() + 3, source.cend(), "bad");
        auto related = handler.make_diagnostic(source.cbegin(), "start");
        EXPECT_EQ(primary.location->source(), related.location->source());
        primary.notes.push_back(diagnostics::DiagnosticNote { related.message, related.location });
        handler.clear_error();
        EXPECT_EQ(handler.source_span(source.cbegin(), source.cend()).source(), primary.location->source());
        handler(source.cbegin() + 3, source.cend(), "bad");
        EXPECT_EQ(output.str(), "In file utf8, line 1:\nbad\né  x\n    ~ <<-- Here\n");
        return primary;
    }();

    ASSERT_TRUE(diagnostic.location);
    EXPECT_EQ(diagnostic.location.value().source()->text(), "é\tx");
    EXPECT_EQ(diagnostics::format_diagnostic(diagnostic),
              "In file utf8, line 1:\nbad\né  x\n    ~ <<-- Here\n"
              "In file utf8, line 1:\nnote: start\né  x\n^_\n");
}

TEST(YggdrasilX3Diagnostics, GrammarContextReportsInnermostStartAndPreservesAttributes)
{
    const auto inner = diagnostics::context(
        ":values")[((boost::spirit::x3::lit('(') >> boost::spirit::x3::lit(":values")) > boost::spirit::x3::int_) > boost::spirit::x3::lit(')')];
    const auto grammar = boost::spirit::x3::rule<NumberRule, Number> { "context_number" } =
        diagnostics::context(":outer")[((boost::spirit::x3::lit('(') >> boost::spirit::x3::lit(":outer")) > inner) > boost::spirit::x3::lit(')')];
    const auto source = std::string("  (:outer\n (:values 42");
    auto first = source.cbegin();
    auto output = std::ostringstream {};
    auto handler = Handler(first, source.cend(), output, "context.txt");
    auto ast = Number {};
    EXPECT_FALSE(boost::spirit::x3::phrase_parse(first,
                                                 source.cend(),
                                                 boost::spirit::x3::with<diagnostics::ErrorHandlerTag>(std::ref(handler))[grammar],
                                                 boost::spirit::x3::ascii::space,
                                                 ast));
    const auto diagnostic = handler.diagnostic("fallback", first);
    EXPECT_NE(diagnostic.message.find("while parsing :values"), std::string::npos);
    ASSERT_TRUE(diagnostic.location);
    EXPECT_EQ(diagnostic.location.value().begin(), source.size());
    ASSERT_EQ(diagnostic.notes.size(), 1);
    EXPECT_EQ(diagnostic.notes.front().message, ":values starts here");
    ASSERT_TRUE(diagnostic.notes.front().location);
    EXPECT_EQ(diagnostic.notes.front().location.value().begin(), source.find("(:values"));
    EXPECT_EQ(output.str(), diagnostics::format_diagnostic(diagnostic));

    const auto valid = std::string("(:outer (:values 42))");
    first = valid.cbegin();
    auto valid_handler = Handler(first, valid.cend(), output);
    ASSERT_TRUE(boost::spirit::x3::phrase_parse(first,
                                                valid.cend(),
                                                boost::spirit::x3::with<diagnostics::ErrorHandlerTag>(std::ref(valid_handler))[grammar],
                                                boost::spirit::x3::ascii::space,
                                                ast));
    EXPECT_EQ(ast.value, 42);
    EXPECT_FALSE(valid_handler.last_error());
    const auto fallback = valid_handler.diagnostic("fallback", first);
    EXPECT_EQ(fallback.message, "fallback");
    EXPECT_TRUE(fallback.notes.empty());
    EXPECT_EQ(fallback.location.value().begin(), valid.size());
}

TEST(YggdrasilX3Diagnostics, FailedChildPointsPastSkippedWhitespaceAndComments)
{
    const auto child = ((boost::spirit::x3::lit('(') >> boost::spirit::x3::lit(":child")) > boost::spirit::x3::int_) > boost::spirit::x3::lit(')');
    const auto body = ((boost::spirit::x3::lit('(') >> boost::spirit::x3::lit(":outer")) > child) > boost::spirit::x3::lit(')');
    const auto skipper =
        boost::spirit::x3::ascii::space | (';' >> *(boost::spirit::x3::char_ - boost::spirit::x3::eol) >> (boost::spirit::x3::eol | boost::spirit::x3::eoi));
    for (const auto contextual : { false, true })
    {
        const auto source = std::string("(:outer \t; ignored parenthesis )\n (:unknown))");
        auto first = source.cbegin();
        auto output = std::ostringstream {};
        auto handler = Handler(first, source.cend(), output);
        auto ast = Number {};
        if (contextual)
        {
            const auto grammar = boost::spirit::x3::rule<NumberRule, Number> { "outer" } = diagnostics::context(":outer")[body];
            EXPECT_FALSE(boost::spirit::x3::phrase_parse(first,
                                                         source.cend(),
                                                         boost::spirit::x3::with<diagnostics::ErrorHandlerTag>(std::ref(handler))[grammar],
                                                         skipper,
                                                         ast));
        }
        else
        {
            const auto grammar = boost::spirit::x3::rule<NumberRule, Number> { "outer" } = body;
            EXPECT_FALSE(boost::spirit::x3::phrase_parse(first,
                                                         source.cend(),
                                                         boost::spirit::x3::with<diagnostics::ErrorHandlerTag>(std::ref(handler))[grammar],
                                                         skipper,
                                                         ast));
        }
        ASSERT_TRUE(handler.last_error());
        EXPECT_EQ(handler.last_error().value().position, source.cbegin() + source.find("(:unknown"));
    }
}

TEST(YggdrasilX3Diagnostics, ExpectedContextUsesItsNameWithoutChangingErrorOwnership)
{
    const auto child = diagnostics::context(
        ":values")[((boost::spirit::x3::lit('(') >> boost::spirit::x3::lit(":values")) > boost::spirit::x3::int_) > boost::spirit::x3::lit(')')];
    EXPECT_EQ(boost::spirit::x3::what(child), ":values");
    const auto grammar = boost::spirit::x3::rule<NumberRule, Number> { "outer" } =
        diagnostics::context(":outer")[((boost::spirit::x3::lit('(') >> boost::spirit::x3::lit(":outer")) > child) > boost::spirit::x3::lit(')')];
    const auto source = std::string("(:outer (:unknown 42))");
    auto first = source.cbegin();
    auto output = std::ostringstream {};
    auto handler = Handler(first, source.cend(), output);
    auto ast = Number {};
    EXPECT_FALSE(boost::spirit::x3::phrase_parse(first,
                                                 source.cend(),
                                                 boost::spirit::x3::with<diagnostics::ErrorHandlerTag>(std::ref(handler))[grammar],
                                                 boost::spirit::x3::ascii::space,
                                                 ast));
    const auto diagnostic = handler.diagnostic("fallback", first);
    EXPECT_EQ(diagnostic.message, "Expected :values while parsing :outer");
    ASSERT_TRUE(diagnostic.location);
    EXPECT_EQ(diagnostic.location.value().begin(), source.find("(:unknown"));
    ASSERT_EQ(diagnostic.notes.size(), 1);
    EXPECT_EQ(diagnostic.notes.front().message, ":outer starts here");
    ASSERT_TRUE(diagnostic.notes.front().location);
    EXPECT_EQ(diagnostic.notes.front().location.value().begin(), 0);
}

}  // namespace ygg::tests
