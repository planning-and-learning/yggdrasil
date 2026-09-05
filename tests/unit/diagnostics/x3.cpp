/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <boost/fusion/include/adapt_struct.hpp>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <sstream>
#include <string>
#include <yggdrasil/diagnostics/x3.hpp>

namespace ygg::tests
{

struct Number : boost::spirit::x3::position_tagged
{
    int value = 0;
};

}  // namespace ygg::tests

BOOST_FUSION_ADAPT_STRUCT(ygg::tests::Number, value)

namespace ygg::tests
{

using namespace diagnostics;
using Handler = ErrorHandler<std::string::const_iterator>;

struct NumberRule : ErrorHandlerBase
{
};
const auto number = x3::rule<NumberRule, Number> { "number" } = '(' > x3::int_ > ')';

TEST(YggdrasilX3Diagnostics, GrammarSuccessTagsAstWithItsSourceRange)
{
    const auto source = std::string(" (42) ");
    auto first = source.cbegin();
    auto output = std::ostringstream {};
    auto handler = Handler(first, source.cend(), output, "numbers", 2);
    auto ast = Number {};
    const auto grammar = x3::with<ErrorHandlerTag>(std::ref(handler))[number];
    ASSERT_TRUE(x3::phrase_parse(first, source.cend(), grammar, x3::ascii::space, ast));
    EXPECT_EQ(first, source.cend());
    EXPECT_EQ(ast.value, 42);
    EXPECT_FALSE(handler.last_error());
    EXPECT_TRUE(output.str().empty());

    const auto diagnostic = handler.make_diagnostic(ast, "number");
    ASSERT_TRUE(diagnostic.location);
    EXPECT_EQ(diagnostic.location->begin(), 1);
    EXPECT_EQ(diagnostic.location->end(), 5);
    EXPECT_EQ(format_diagnostic(diagnostic), "In file numbers, line 1:\nnumber\n (42) \n ~~~~ <<-- Here\n");
    EXPECT_EQ(format_error_at(handler, ast, "number"), format_error_at(handler, source.cbegin() + 1, "number"));
    handler(ast, "number");
    EXPECT_EQ(output.str(), format_diagnostic(diagnostic));
}

TEST(YggdrasilX3Diagnostics, GrammarExpectationFailureRecordsAndRendersEof)
{
    const auto source = std::string("(42");
    auto first = source.cbegin();
    auto output = std::ostringstream {};
    auto handler = Handler(first, source.cend(), output);
    auto ast = Number {};
    const auto grammar = x3::with<ErrorHandlerTag>(std::ref(handler))[number];
    EXPECT_FALSE(x3::phrase_parse(first, source.cend(), grammar, x3::ascii::space, ast));
    ASSERT_TRUE(handler.last_error());
    EXPECT_EQ(handler.last_error()->position, source.cend());
    EXPECT_NE(handler.last_error()->message.find("Expecting:"), std::string::npos);
    EXPECT_NE(handler.last_error()->message.find(')'), std::string::npos);
    EXPECT_EQ(output.str(), format_error_at(handler, source.cend(), handler.last_error()->message));
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
    EXPECT_EQ(handler.last_error()->position, source.cbegin());
    EXPECT_EQ(handler.last_error()->message, "first");
    EXPECT_TRUE(output.str().empty());
    handler.clear_error();
    EXPECT_FALSE(handler.last_error());
    handler.record_error(source.cend(), "next");
    ASSERT_TRUE(handler.last_error());
    EXPECT_EQ(handler.last_error()->position, source.cend());
    EXPECT_EQ(handler.last_error()->message, "next");
}

TEST(YggdrasilX3Diagnostics, UntaggedNodesHaveNoLocation)
{
    const auto source = std::string("");
    auto output = std::ostringstream {};
    auto handler = Handler(source.cbegin(), source.cend(), output);
    const auto node = x3::position_tagged {};
    const auto diagnostic = handler.make_diagnostic(node, "fallback");
    EXPECT_FALSE(diagnostic.location);
    EXPECT_EQ(diagnostic.message, "fallback");
    EXPECT_EQ(format_error_at(handler, node, "fallback"), "fallback");
    handler(node, "fallback");
    EXPECT_EQ(output.str(), "fallback\n");
    EXPECT_EQ(format_error_at(handler, source.cend(), "empty"), "In line 1:\nempty\n\n^_\n");
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
        primary.notes.push_back(DiagnosticNote { related.message, related.location });
        handler.clear_error();
        EXPECT_EQ(handler.source_span(source.cbegin(), source.cend()).source(), primary.location->source());
        handler(source.cbegin() + 3, source.cend(), "bad");
        EXPECT_EQ(output.str(), "In file utf8, line 1:\nbad\né  x\n    ~ <<-- Here\n");
        return primary;
    }();

    ASSERT_TRUE(diagnostic.location);
    EXPECT_EQ(diagnostic.location->source()->text(), "é\tx");
    EXPECT_EQ(format_diagnostic(diagnostic),
              "In file utf8, line 1:\nbad\né  x\n    ~ <<-- Here\n"
              "In file utf8, line 1:\nnote: start\né  x\n^_\n");
}

TEST(YggdrasilX3Diagnostics, GrammarContextReportsInnermostStartAndPreservesAttributes)
{
    const auto inner = diagnostics::context(":values")[x3::lit('(') >> x3::lit(":values") > x3::int_ > x3::lit(')')];
    const auto grammar = x3::rule<NumberRule, Number> { "context_number" } =
        diagnostics::context(":outer")[x3::lit('(') >> x3::lit(":outer") > inner > x3::lit(')')];
    const auto source = std::string("  (:outer\n (:values 42");
    auto first = source.cbegin();
    auto output = std::ostringstream {};
    auto handler = Handler(first, source.cend(), output, "context.txt");
    auto ast = Number {};
    EXPECT_FALSE(x3::phrase_parse(first, source.cend(), x3::with<ErrorHandlerTag>(std::ref(handler))[grammar], x3::ascii::space, ast));
    const auto diagnostic = handler.diagnostic("fallback", first);
    EXPECT_NE(diagnostic.message.find("while parsing :values"), std::string::npos);
    ASSERT_TRUE(diagnostic.location);
    EXPECT_EQ(diagnostic.location->begin(), source.size());
    ASSERT_EQ(diagnostic.notes.size(), 1);
    EXPECT_EQ(diagnostic.notes.front().message, ":values starts here");
    ASSERT_TRUE(diagnostic.notes.front().location);
    EXPECT_EQ(diagnostic.notes.front().location->begin(), source.find("(:values"));
    EXPECT_EQ(output.str(), format_diagnostic(diagnostic));

    const auto valid = std::string("(:outer (:values 42))");
    first = valid.cbegin();
    auto valid_handler = Handler(first, valid.cend(), output);
    ASSERT_TRUE(x3::phrase_parse(first, valid.cend(), x3::with<ErrorHandlerTag>(std::ref(valid_handler))[grammar], x3::ascii::space, ast));
    EXPECT_EQ(ast.value, 42);
    EXPECT_FALSE(valid_handler.last_error());
    const auto fallback = valid_handler.diagnostic("fallback", first);
    EXPECT_EQ(fallback.message, "fallback");
    EXPECT_TRUE(fallback.notes.empty());
    EXPECT_EQ(fallback.location->begin(), valid.size());
}

TEST(YggdrasilX3Diagnostics, FailedChildPointsPastSkippedWhitespaceAndComments)
{
    const auto child = x3::lit('(') >> x3::lit(":child") > x3::int_ > x3::lit(')');
    const auto body = x3::lit('(') >> x3::lit(":outer") > child > x3::lit(')');
    const auto skipper = x3::ascii::space | (';' >> *(x3::char_ - x3::eol) >> (x3::eol | x3::eoi));
    for (const auto contextual : { false, true })
    {
        const auto source = std::string("(:outer \t; ignored parenthesis )\n (:unknown))");
        auto first = source.cbegin();
        auto output = std::ostringstream {};
        auto handler = Handler(first, source.cend(), output);
        auto ast = Number {};
        if (contextual)
        {
            const auto grammar = x3::rule<NumberRule, Number> { "outer" } = diagnostics::context(":outer")[body];
            EXPECT_FALSE(x3::phrase_parse(first, source.cend(), x3::with<ErrorHandlerTag>(std::ref(handler))[grammar], skipper, ast));
        }
        else
        {
            const auto grammar = x3::rule<NumberRule, Number> { "outer" } = body;
            EXPECT_FALSE(x3::phrase_parse(first, source.cend(), x3::with<ErrorHandlerTag>(std::ref(handler))[grammar], skipper, ast));
        }
        ASSERT_TRUE(handler.last_error());
        EXPECT_EQ(handler.last_error()->position, source.cbegin() + source.find("(:unknown"));
    }
}

TEST(YggdrasilX3Diagnostics, ExpectedContextUsesItsNameWithoutChangingErrorOwnership)
{
    const auto child = diagnostics::context(":values")[x3::lit('(') >> x3::lit(":values") > x3::int_ > x3::lit(')')];
    EXPECT_EQ(x3::what(child), ":values");
    const auto grammar = x3::rule<NumberRule, Number> { "outer" } = diagnostics::context(":outer")[x3::lit('(') >> x3::lit(":outer") > child > x3::lit(')')];
    const auto source = std::string("(:outer (:unknown 42))");
    auto first = source.cbegin();
    auto output = std::ostringstream {};
    auto handler = Handler(first, source.cend(), output);
    auto ast = Number {};
    EXPECT_FALSE(x3::phrase_parse(first, source.cend(), x3::with<ErrorHandlerTag>(std::ref(handler))[grammar], x3::ascii::space, ast));
    const auto diagnostic = handler.diagnostic("fallback", first);
    EXPECT_EQ(diagnostic.message, "Expected :values while parsing :outer");
    ASSERT_TRUE(diagnostic.location);
    EXPECT_EQ(diagnostic.location->begin(), source.find("(:unknown"));
    ASSERT_EQ(diagnostic.notes.size(), 1);
    EXPECT_EQ(diagnostic.notes.front().message, ":outer starts here");
    ASSERT_TRUE(diagnostic.notes.front().location);
    EXPECT_EQ(diagnostic.notes.front().location->begin(), 0);
}

}  // namespace ygg::tests
