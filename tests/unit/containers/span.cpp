/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/containers/span.hpp"

#include <array>
#include <concepts>
#include <gtest/gtest.h>
#include <ranges>
#include <span>
#include <stdexcept>

namespace ygg::tests
{
struct SpanValue
{
    int value;
};
struct ThrowingSpanContext
{
    const ThrowingSpanContext& get_canonical_context(const SpanValue&) const { throw std::out_of_range("missing value"); }
};
}

namespace ygg
{
template<>
class View<tests::SpanValue, tests::ThrowingSpanContext>
{
    const tests::SpanValue* m_handle;
    const tests::ThrowingSpanContext* m_context;

public:
    View(const tests::SpanValue& handle, const tests::ThrowingSpanContext& context) noexcept : m_handle(&handle), m_context(&context) {}
    const auto& get_handle() const noexcept { return *m_handle; }
    const auto& get_data() const noexcept { return *m_handle; }
    const auto& get_context() const noexcept { return *m_context; }
};
}

namespace ygg::tests
{
TEST(YggdrasilTests, SpanViewBorrowsElementsAndOwnsTemporarySpanHandles)
{
    const auto context = 0;
    auto values = std::array { 2, 4, 6 };
    const auto view = make_view(std::span(values), context);
    static_assert(ViewConcept<std::span<int, 3>, int>);
    static_assert(std::same_as<decltype(view[0]), const int&>);
    static_assert(std::same_as<decltype(view.get_data()), std::span<const int, 3>>);
    static_assert(std::ranges::random_access_range<decltype(view)>);
    static_assert(std::ranges::borrowed_range<decltype(view)>);
    EXPECT_EQ(view.size(), 3);
    EXPECT_EQ(view.front(), 2);
    EXPECT_EQ(view.back(), 6);
    EXPECT_EQ(view.at(1), 4);
    EXPECT_THROW(view.at(3), std::out_of_range);
    EXPECT_EQ(view.data(), values.data());
    values[1] = 9;
    EXPECT_EQ(view[1], 9);

    auto first = make_view(std::span<const int>(values), context).begin();
    const auto last = make_view(std::span<const int>(values), context).end();
    EXPECT_EQ(last - first, 3);
    EXPECT_EQ(first[1], 9);
    EXPECT_EQ(*(2 + first), 6);
    EXPECT_LT(first, last);
    first += 2;
    EXPECT_EQ(*first--, 6);
    EXPECT_EQ(*first, 9);
    EXPECT_EQ(*--first, 2);
    EXPECT_EQ(view.cbegin(), view.begin());
    EXPECT_EQ(view.cend(), view.end());

    const auto empty = make_view(std::span<const int>(), context);
    EXPECT_TRUE(empty.empty());
    EXPECT_EQ(empty.begin(), empty.end());
    EXPECT_EQ(empty.end() - empty.begin(), 0);
    EXPECT_THROW(empty.at(0), std::out_of_range);
}

TEST(YggdrasilTests, SpanViewPropagatesCanonicalLookupErrors)
{
    const auto context = ThrowingSpanContext {};
    const auto values = std::array { SpanValue { 1 } };
    const auto view = make_view(std::span(values), context);
    static_assert(ViewConcept<SpanValue, ThrowingSpanContext>);
    EXPECT_THROW((void) view[0], std::out_of_range);
    EXPECT_THROW((void) view.at(0), std::out_of_range);
    EXPECT_THROW((void) view.front(), std::out_of_range);
    EXPECT_THROW((void) view.back(), std::out_of_range);
    EXPECT_THROW((void) *view.begin(), std::out_of_range);
    EXPECT_THROW((void) view.begin()[0], std::out_of_range);
}
}
