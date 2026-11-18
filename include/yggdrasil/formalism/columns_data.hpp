/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_COLUMNS_DATA_HPP_
#define YGG_FORMALISM_COLUMNS_DATA_HPP_

#include "yggdrasil/formalism/column_layout.hpp"
#include "yggdrasil/formalism/declarations.hpp"

#include <urd/elements/flat.hpp>
#include <utility>

namespace urd
{
/// The columns of a relation, labeled and typed.
template<>
struct data<ygg::Columns>
{
    urd::index<ygg::Columns> index;
    urd::flat_vector<ygg::ColumnLayout> values;

    data() = default;

    explicit data(urd::flat_vector<ygg::ColumnLayout> values_) : index(), values(std::move(values_)) {}

    data(const data& other) = delete;
    data& operator=(const data& other) = delete;
    data(data&& other) = default;
    data& operator=(data&& other) = default;
    ~data() = default;

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"values", &data::values>> {}; }

    static constexpr auto identifying_members() noexcept { return urd::members_of(fields()); }

    static constexpr auto cista_members() noexcept { return urd::Members<&data::index, &data::values> {}; }
};

}  // namespace urd

#endif
