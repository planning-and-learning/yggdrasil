/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/operators/relational/common.hpp"

namespace ygg::database::detail
{
void append_fields(std::vector<std::byte>& output, std::span<const std::byte> input, std::span<const ColumnSlice> fields)
{
    for (const auto field : fields)
    {
        const auto bytes = input.subspan(field.offset, field.size);
        output.insert(output.end(), bytes.begin(), bytes.end());
    }
}
}

namespace ygg::database::detail
{
void prepare_output(Builder<Relation>& out, std::span<const ColumnLayout> columns, std::initializer_list<const void*> inputs)
{
    if (!std::ranges::equal(out.columns().span(), columns))
        throw std::invalid_argument("Relational operation: output schema does not match.");
    for (const auto* input : inputs)
        if (input == out.get_storage_address())
            throw std::invalid_argument("Relational operation: output aliases an input.");
    out.clear();
}
}
