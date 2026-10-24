/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/details/join_index.hpp"

namespace ygg::database
{
namespace detail
{
hash_t join_key_hash(std::span<const std::byte> row, std::span<const ColumnSlice> keys) noexcept
{
    auto hash = boost::hash2::xxhash_64 {};
    for (const auto key : keys)
        hash.update(row.data() + key.offset, key.size);
    return hash.result();
}
}
}

namespace ygg::database
{
namespace detail
{
bool join_keys_equal(std::span<const std::byte> lhs,
                     std::span<const ColumnSlice> lhs_keys,
                     std::span<const std::byte> rhs,
                     std::span<const ColumnSlice> rhs_keys) noexcept
{
    for (size_t i = 0; i < lhs_keys.size(); ++i)
        if (!std::ranges::equal(lhs.subspan(lhs_keys[i].offset, lhs_keys[i].size), rhs.subspan(rhs_keys[i].offset, rhs_keys[i].size)))
            return false;
    return true;
}
}
}

namespace ygg::database
{
namespace detail
{
void require_key_slices(std::span<const ColumnLayout> columns, std::span<const ColumnSlice> keys)
{
    for (const auto key : keys)
        if (std::ranges::none_of(columns, [&](const auto& column) { return column_slice(column) == key; }))
            throw std::out_of_range("JoinIndex: key does not identify a field in the relation schema.");
}
}
}
