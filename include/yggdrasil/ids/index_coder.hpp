/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_IDS_INDEX_CODER_HPP_
#define YGG_IDS_INDEX_CODER_HPP_

#include "yggdrasil/core/config.hpp"

#include <concepts>
#include <limits>
#include <stdexcept>

namespace ygg
{
/// Encode typed indices in a block, rejecting narrowing that would lose identity.
template<typename I, std::unsigned_integral Block = uint_t>
struct IndexCoder
{
    using value_type = I;
    using index_value_type = typename I::value_type;
    static_assert(std::unsigned_integral<index_value_type>);

    static constexpr value_type decode(Block block) noexcept { return value_type(static_cast<index_value_type>(block)); }
    static constexpr Block encode(value_type value) noexcept(std::numeric_limits<Block>::digits >= std::numeric_limits<index_value_type>::digits)
    {
        if constexpr (std::numeric_limits<Block>::digits < std::numeric_limits<index_value_type>::digits)
            if (value.get_value() > std::numeric_limits<Block>::max())
                throw std::out_of_range("IndexCoder: index exceeds the block value range.");
        return static_cast<Block>(value.get_value());
    }
};
}

#endif
