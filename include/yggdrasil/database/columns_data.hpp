/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_COLUMNS_DATA_HPP_
#define YGG_DATABASE_COLUMNS_DATA_HPP_

#include "yggdrasil/core/types_utils.hpp"
#include "yggdrasil/database/columns_index.hpp"

#include <cista/containers/vector.h>
#include <tuple>

namespace ygg
{
template<>
struct Data<database::Columns>
{
    Index<database::Columns> index;
    IndexList<database::Column> values;

    void clear() noexcept
    {
        ygg::clear(index);
        values.clear();
    }

    auto cista_members() noexcept { return std::tie(index, values); }
    auto cista_members() const noexcept { return std::tie(index, values); }
    auto identifying_members() const noexcept { return std::tie(values); }
};
}  // namespace ygg

#endif
