/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/core/bytes.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace ygg::tests
{
namespace
{
struct ByteValue
{
    uint64_t value;
    ByteValue() = delete;
    explicit ByteValue(uint64_t value_) : value(value_) {}
    const ByteValue* operator&() const noexcept { return nullptr; }
};

template<typename T>
concept CanCopyBytes = requires(const T& value, std::span<std::byte> bytes) {
    store_unaligned(bytes, value);
    { load_unaligned<T>(bytes) } -> std::same_as<T>;
};

static_assert(TriviallyCopyable<ByteValue> && !std::is_default_constructible_v<ByteValue>);
static_assert(CanCopyBytes<uint64_t> && CanCopyBytes<double> && CanCopyBytes<ByteValue>);
static_assert(!CanCopyBytes<std::string>);
}  // namespace

TEST(YggdrasilTests, CommonBytesCopiesUnalignedNativeRepresentation)
{
    alignas(uint64_t) std::array<std::byte, sizeof(uint64_t) + 2> storage;
    storage.fill(std::byte { 0xA5 });
    const auto bytes = std::span(storage).subspan(1, sizeof(uint64_t));
    constexpr auto value = uint64_t { 0x0123456789ABCDEF };

    store_unaligned(bytes, value);

    EXPECT_EQ(load_unaligned<uint64_t>(bytes), value);
    EXPECT_TRUE(std::ranges::equal(bytes, std::bit_cast<std::array<std::byte, sizeof(value)>>(value)));
    EXPECT_EQ(storage.front(), std::byte { 0xA5 });
    EXPECT_EQ(storage.back(), std::byte { 0xA5 });
}

TEST(YggdrasilTests, CommonBytesPreservesFloatingPointRepresentation)
{
    alignas(double) std::array<std::byte, sizeof(double) + 1> storage {};
    const auto bytes = std::span(storage).subspan(1);
    for (const auto value : std::array { -0.0, 42.5, std::numeric_limits<double>::quiet_NaN() })
    {
        store_unaligned(bytes, value);
        const auto restored = load_unaligned<double>(bytes);
        EXPECT_EQ((std::bit_cast<std::array<std::byte, sizeof(double)>>(restored)), (std::bit_cast<std::array<std::byte, sizeof(double)>>(value)));
    }
}

TEST(YggdrasilTests, CommonBytesNeedsNeitherDefaultConstructionNorBuiltinAddressOf)
{
    alignas(ByteValue) std::array<std::byte, sizeof(ByteValue) + 1> storage {};
    const auto bytes = std::span(storage).subspan(1);
    const auto value = ByteValue(123);

    store_unaligned(bytes, value);

    EXPECT_EQ(load_unaligned<ByteValue>(bytes).value, value.value);
}

TEST(YggdrasilTests, CommonBytesRejectsIncorrectWidthsBeforeWriting)
{
    auto storage = std::array<std::byte, sizeof(uint64_t) + 1> {};
    storage.fill(std::byte { 0xA5 });
    const auto original = storage;
    for (const auto size : std::array { size_t { 0 }, sizeof(uint64_t) - 1, sizeof(uint64_t) + 1 })
    {
        const auto bytes = std::span(storage).first(size);
        EXPECT_THROW((void) load_unaligned<uint64_t>(bytes), std::invalid_argument);
        EXPECT_THROW(store_unaligned(bytes, uint64_t { 123 }), std::invalid_argument);
        EXPECT_EQ(storage, original);
    }
}
}  // namespace ygg::tests
