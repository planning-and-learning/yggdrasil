/*
 * Copyright (C) 2025-2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef YGG_CORE_ATOMIC_BIT_HPP_
#define YGG_CORE_ATOMIC_BIT_HPP_

#include "yggdrasil/core/bit.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace ygg::bit
{

template<std::unsigned_integral Block>
Block atomic_load(const Block* block) noexcept
{
    return std::atomic_ref<Block>(*const_cast<Block*>(block)).load(std::memory_order_relaxed);
}

template<std::unsigned_integral Block>
void atomic_replace_bits(Block* block, Block mask, Block value) noexcept
{
    auto reference = std::atomic_ref<Block>(*block);
    auto current = reference.load(std::memory_order_relaxed);
    while (!reference.compare_exchange_weak(current, static_cast<Block>((current & ~mask) | (value & mask)), std::memory_order_relaxed)) {}
}

template<std::unsigned_integral Block>
Block atomic_read_int(const Block* word, uint8_t offset, uint8_t len) noexcept
{
    constexpr std::size_t digits = std::numeric_limits<Block>::digits;

    static_assert(is_power_of_two(digits), "Block width must be a power of two.");
    assert(len <= digits && "Width exceeds block width.");
    assert(offset < digits && "Offset must lie within one block.");

    const Block first = atomic_load(word) >> offset;
    if (offset + len > digits)
        return first | ((atomic_load(word + 1) & lo_set<Block>[(offset + len) & (digits - 1)]) << (digits - offset));
    return first & lo_set<Block>[len];
}

template<std::unsigned_integral Block>
void atomic_write_int(Block* word, Block value, uint8_t offset, uint8_t len) noexcept
{
    constexpr std::size_t digits = std::numeric_limits<Block>::digits;

    static_assert(is_power_of_two(digits), "Block width must be a power of two.");
    assert(len <= digits && "Width exceeds block width.");
    assert(offset < digits && "Offset must lie within one block.");

    value &= lo_set<Block>[len];

    const auto first_len = static_cast<uint8_t>(std::min<size_t>(len, digits - offset));
    const auto first_mask = static_cast<Block>(lo_set<Block>[first_len] << offset);
    atomic_replace_bits(word, first_mask, static_cast<Block>(value << offset));

    if (first_len < len)
    {
        const auto second_len = static_cast<uint8_t>(len - first_len);
        atomic_replace_bits(word + 1, lo_set<Block>[second_len], static_cast<Block>(value >> first_len));
    }
}

/// Provides race-free packed access using relaxed atomic block operations.
/// A value spanning two blocks is not read or written as one atomic unit.
template<std::unsigned_integral Block, BlockCoder<Block> Coder = ForwardingBlockCoder<Block>>
class atomic_int_reference
{
public:
    using value_type = typename Coder::value_type;

    atomic_int_reference(Block* word, uint8_t offset, uint8_t len) noexcept : m_word(word), m_offset(offset), m_len(len)
    {
        constexpr std::size_t digits = std::numeric_limits<Block>::digits;
        static_assert(is_power_of_two(digits), "Block width must be a power of two.");
        assert(len <= digits && "Width exceeds block width.");
        assert(offset < digits && "Offset must lie within one block.");
    }

    atomic_int_reference& operator=(const value_type& value)
    {
        const auto raw = Coder::encode(value);
        if ((raw & ~lo_set<Block>[m_len]) != 0)
            throw std::out_of_range("atomic_int_reference: encoded value exceeds bit width");
        atomic_write_int(m_word, raw, m_offset, m_len);
        return *this;
    }

    atomic_int_reference& operator=(const atomic_int_reference& other) { return *this = static_cast<value_type>(other); }

    operator value_type() const { return Coder::decode(atomic_read_int(m_word, m_offset, m_len)); }

private:
    Block* m_word;
    uint8_t m_offset;
    uint8_t m_len;
};

}  // namespace ygg::bit

#endif
