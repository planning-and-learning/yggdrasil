/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "yggdrasil/diagnostics/diagnostic.hpp"

#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <string>

namespace ygg::tests
{

TEST(YggdrasilDiagnostics, ValidatesSourceAndSpanBoundaries)
{
    const auto source = std::make_shared<const diagnostics::Source>("abc", "input", 2);
    EXPECT_EQ(source->text(), "abc");
    EXPECT_EQ(source->filename(), "input");
    EXPECT_EQ(source->tab_width(), 2);
    EXPECT_THROW((diagnostics::Source("", "", 0)), std::invalid_argument);
    EXPECT_THROW((diagnostics::Source("", "", -1)), std::invalid_argument);
    EXPECT_THROW((diagnostics::SourceSpan(nullptr, 0, 0)), std::invalid_argument);
    EXPECT_THROW((diagnostics::SourceSpan(source, 2, 1)), std::invalid_argument);
    EXPECT_THROW((diagnostics::SourceSpan(source, 0, 4)), std::invalid_argument);
    EXPECT_THROW((diagnostics::SourceSpan(source, 0, std::numeric_limits<std::size_t>::max())), std::invalid_argument);

    const auto full = diagnostics::SourceSpan(source, 0, 3);
    EXPECT_EQ(full.source(), source);
    EXPECT_EQ(full.begin(), 0);
    EXPECT_EQ(full.end(), 3);
    const auto eof = diagnostics::SourceSpan(source, 3, 3);
    EXPECT_EQ(eof.line(), 1);
    EXPECT_EQ(eof.column(), 4);
}

TEST(YggdrasilDiagnostics, PreservesUtf8AndUsesByteOffsets)
{
    const auto source = std::make_shared<const diagnostics::Source>("é\tb");
    const auto span = diagnostics::SourceSpan(source, 3, 4);
    EXPECT_EQ(span.line(), 1);
    EXPECT_EQ(span.column(), 4);
    EXPECT_EQ(source->text().substr(span.begin(), span.end() - span.begin()), "b");
    EXPECT_EQ(diagnostics::format_diagnostic(diagnostics::Diagnostic { "bad", span }), "In line 1:\nbad\né    b\n      ~ <<-- Here\n");
}

TEST(YggdrasilDiagnostics, RecognizesLineEndingsAndEof)
{
    for (const auto& ending : { std::string("\n"), std::string("\r\n"), std::string("\r") })
    {
        SCOPED_TRACE(ending);
        auto text = std::string("a");
        text += ending;
        text += 'b';
        text += ending;
        const auto source = std::make_shared<const diagnostics::Source>(std::move(text));
        const auto newline = diagnostics::SourceSpan(source, 1, 1);
        EXPECT_EQ(newline.line(), 1);
        EXPECT_EQ(newline.column(), 2);
        const auto second_line = diagnostics::SourceSpan(source, 1 + ending.size(), 2 + ending.size());
        EXPECT_EQ(second_line.line(), 2);
        EXPECT_EQ(second_line.column(), 1);
        EXPECT_EQ(diagnostics::format_diagnostic(diagnostics::Diagnostic { "bad", second_line }), "In line 2:\nbad\nb\n~ <<-- Here\n");
        const auto eof = diagnostics::SourceSpan(source, source->text().size(), source->text().size());
        EXPECT_EQ(eof.line(), 3);
        EXPECT_EQ(eof.column(), 1);
        EXPECT_EQ(diagnostics::format_diagnostic(diagnostics::Diagnostic { "missing", eof }), "In line 3:\nmissing\n\n^_\n");
    }

    const auto source = std::make_shared<const diagnostics::Source>("");
    const auto empty = diagnostics::SourceSpan(source, 0, 0);
    EXPECT_EQ(empty.line(), 1);
    EXPECT_EQ(empty.column(), 1);
    EXPECT_EQ(diagnostics::format_diagnostic(diagnostics::Diagnostic { "missing", empty }), "In line 1:\nmissing\n\n^_\n");
}

TEST(YggdrasilDiagnostics, RendersPointsRangesAndTabs)
{
    const auto source = std::make_shared<const diagnostics::Source>("a\tbc\nsecond", "input", 2);
    EXPECT_EQ(diagnostics::format_diagnostic(diagnostics::Diagnostic { "point", diagnostics::SourceSpan(source, 2, 2) }),
              "In file input, line 1:\npoint\na  bc\n___^_\n");
    EXPECT_EQ(diagnostics::format_diagnostic(diagnostics::Diagnostic { "range", diagnostics::SourceSpan(source, 2, 4) }),
              "In file input, line 1:\nrange\na  bc\n   ~~ <<-- Here\n");
    EXPECT_EQ(diagnostics::format_diagnostic(diagnostics::Diagnostic { "tab", diagnostics::SourceSpan(source, 1, 2) }),
              "In file input, line 1:\ntab\na  bc\n ~~ <<-- Here\n");
    EXPECT_EQ(diagnostics::format_diagnostic(diagnostics::Diagnostic { "range", diagnostics::SourceSpan(source, 2, source->text().size()) }),
              "In file input, line 1:\nrange\na  bc\n   ~~ <<-- Here\n");
    const auto eof_source = std::make_shared<const diagnostics::Source>("abc");
    EXPECT_EQ(diagnostics::format_diagnostic(diagnostics::Diagnostic { "eof", diagnostics::SourceSpan(eof_source, 3, 3) }), "In line 1:\neof\nabc\n___^_\n");
}

TEST(YggdrasilDiagnostics, OwnsPrimaryAndRelatedSourcesAfterTheirCreatorsReturn)
{
    const auto diagnostic = []
    {
        const auto policy = std::make_shared<const diagnostics::Source>("(p)", "policy.sketch");
        const auto domain = std::make_shared<const diagnostics::Source>("(domain)", "domain.pddl");
        return diagnostics::Diagnostic { "undefined predicate",
                                         diagnostics::SourceSpan(policy, 1, 2),
                                         { diagnostics::DiagnosticNote { "domain declared here", diagnostics::SourceSpan(domain, 1, 7) },
                                           diagnostics::DiagnosticNote { "check spelling" } } };
    }();

    ASSERT_TRUE(diagnostic.location);
    ASSERT_TRUE(diagnostic.notes.front().location);
    EXPECT_NE(diagnostic.location->source(), diagnostic.notes.front().location->source());
    EXPECT_EQ(diagnostics::format_diagnostic(diagnostic),
              "In file policy.sketch, line 1:\nundefined predicate\n(p)\n ~ <<-- Here\n"
              "In file domain.pddl, line 1:\nnote: domain declared here\n(domain)\n ~~~~~~ <<-- Here\n"
              "note: check spelling\n");
    EXPECT_EQ(diagnostics::format_diagnostic(diagnostics::Diagnostic { "no source", std::nullopt, { diagnostics::DiagnosticNote { "also no source" } } }),
              "no source\nnote: also no source\n");
}

}  // namespace ygg::tests
