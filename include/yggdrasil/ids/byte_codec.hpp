/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_IDS_BYTE_CODEC_HPP_
#define YGG_IDS_BYTE_CODEC_HPP_

#include "yggdrasil/core/byte_codec.hpp"
#include "yggdrasil/core/types.hpp"
#include "yggdrasil/ids/index_coder.hpp"

#include <concepts>
#include <cstddef>
#include <span>

namespace ygg
{

/// Indices encode as their coded integer; the id distinguishes index types by tag.
template<typename Tag>
    requires requires(Index<Tag> index) { { index.get_value() } -> std::same_as<uint_t>; }
struct ByteCodec<Index<Tag>>
{
    static constexpr TypeId id = type_name_hash<Index<Tag>>();
    static constexpr std::size_t size = ByteCodec<uint_t>::size;

    static void encode(Index<Tag> value, std::span<std::byte> bytes) { ByteCodec<uint_t>::encode(IndexCoder<Index<Tag>>::encode(value), bytes); }
    static Index<Tag> decode(std::span<const std::byte> bytes) { return IndexCoder<Index<Tag>>::decode(ByteCodec<uint_t>::decode(bytes)); }
};

}  // namespace ygg

#endif
