/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <string>
#include <yggdrasil/diagnostics/diagnostic.hpp>

namespace ygg::tests
{

using namespace diagnostics;

TEST(YggdrasilDiagnostics, ValidatesSourceAndSpanBoundaries)
{
    const auto source = std::make_shared<const Source>("abc", "input", 2);
    EXPECT_EQ(source->text(), "abc");
    EXPECT_EQ(source->filename(), "input");
    EXPECT_EQ(source->tab_width(), 2);
    EXPECT_THROW((Source("", "", 0)), std::invalid_argument);
    EXPECT_THROW((Source("", "", -1)), std::invalid_argument);
    EXPECT_THROW((SourceSpan(nullptr, 0, 0)), std::invalid_argument);
    EXPECT_THROW((SourceSpan(source, 2, 1)), std::invalid_argument);
    EXPECT_THROW((SourceSpan(source, 0, 4)), std::invalid_argument);
    EXPECT_THROW((SourceSpan(source, 0, std::numeric_limits<std::size_t>::max())), std::invalid_argument);

    const auto full = SourceSpan(source, 0, 3);
    EXPECT_EQ(full.source(), source);
    EXPECT_EQ(full.begin(), 0);
    EXPECT_EQ(full.end(), 3);
    const auto eof = SourceSpan(source, 3, 3);
    EXPECT_EQ(eof.line(), 1);
    EXPECT_EQ(eof.column(), 4);
}

TEST(YggdrasilDiagnostics, PreservesUtf8AndUsesByteOffsets)
{
    const auto source = std::make_shared<const Source>("é\tb");
    const auto span = SourceSpan(source, 3, 4);
    EXPECT_EQ(span.line(), 1);
    EXPECT_EQ(span.column(), 4);
    EXPECT_EQ(source->text().substr(span.begin(), span.end() - span.begin()), "b");
    EXPECT_EQ(format_diagnostic(Diagnostic { "bad", span }), "In line 1:\nbad\né    b\n      ~ <<-- Here\n");
}

TEST(YggdrasilDiagnostics, RecognizesLineEndingsAndEof)
{
    for (const auto& ending : { std::string("\n"), std::string("\r\n"), std::string("\r") })
    {
        SCOPED_TRACE(ending);
        const auto source = std::make_shared<const Source>("a" + ending + "b" + ending);
        const auto newline = SourceSpan(source, 1, 1);
        EXPECT_EQ(newline.line(), 1);
        EXPECT_EQ(newline.column(), 2);
        const auto second_line = SourceSpan(source, 1 + ending.size(), 2 + ending.size());
        EXPECT_EQ(second_line.line(), 2);
        EXPECT_EQ(second_line.column(), 1);
        EXPECT_EQ(format_diagnostic(Diagnostic { "bad", second_line }), "In line 2:\nbad\nb\n~ <<-- Here\n");
        const auto eof = SourceSpan(source, source->text().size(), source->text().size());
        EXPECT_EQ(eof.line(), 3);
        EXPECT_EQ(eof.column(), 1);
        EXPECT_EQ(format_diagnostic(Diagnostic { "missing", eof }), "In line 3:\nmissing\n\n^_\n");
    }

    const auto source = std::make_shared<const Source>("");
    const auto empty = SourceSpan(source, 0, 0);
    EXPECT_EQ(empty.line(), 1);
    EXPECT_EQ(empty.column(), 1);
    EXPECT_EQ(format_diagnostic(Diagnostic { "missing", empty }), "In line 1:\nmissing\n\n^_\n");
}

TEST(YggdrasilDiagnostics, RendersPointsRangesAndTabs)
{
    const auto source = std::make_shared<const Source>("a\tbc\nsecond", "input", 2);
    EXPECT_EQ(format_diagnostic(Diagnostic { "point", SourceSpan(source, 2, 2) }), "In file input, line 1:\npoint\na  bc\n___^_\n");
    EXPECT_EQ(format_diagnostic(Diagnostic { "range", SourceSpan(source, 2, 4) }), "In file input, line 1:\nrange\na  bc\n   ~~ <<-- Here\n");
    EXPECT_EQ(format_diagnostic(Diagnostic { "tab", SourceSpan(source, 1, 2) }), "In file input, line 1:\ntab\na  bc\n ~~ <<-- Here\n");
    EXPECT_EQ(format_diagnostic(Diagnostic { "range", SourceSpan(source, 2, source->text().size()) }),
              "In file input, line 1:\nrange\na  bc\n   ~~ <<-- Here\n");
    const auto eof_source = std::make_shared<const Source>("abc");
    EXPECT_EQ(format_diagnostic(Diagnostic { "eof", SourceSpan(eof_source, 3, 3) }), "In line 1:\neof\nabc\n___^_\n");
}

TEST(YggdrasilDiagnostics, OwnsPrimaryAndRelatedSourcesAfterTheirCreatorsReturn)
{
    const auto diagnostic = []
    {
        const auto policy = std::make_shared<const Source>("(p)", "policy.sketch");
        const auto domain = std::make_shared<const Source>("(domain)", "domain.pddl");
        return Diagnostic { "undefined predicate",
                            SourceSpan(policy, 1, 2),
                            { DiagnosticNote { "domain declared here", SourceSpan(domain, 1, 7) }, DiagnosticNote { "check spelling" } } };
    }();

    ASSERT_TRUE(diagnostic.location);
    ASSERT_TRUE(diagnostic.notes.front().location);
    EXPECT_NE(diagnostic.location->source(), diagnostic.notes.front().location->source());
    EXPECT_EQ(format_diagnostic(diagnostic),
              "In file policy.sketch, line 1:\nundefined predicate\n(p)\n ~ <<-- Here\n"
              "In file domain.pddl, line 1:\nnote: domain declared here\n(domain)\n ~~~~~~ <<-- Here\n"
              "note: check spelling\n");
    EXPECT_EQ(format_diagnostic(Diagnostic { "no source", std::nullopt, { DiagnosticNote { "also no source" } } }), "no source\nnote: also no source\n");
}

}  // namespace ygg::tests
