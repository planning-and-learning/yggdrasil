/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_COLUMNS_BUILDER_HPP_
#define YGG_FORMALISM_COLUMNS_BUILDER_HPP_

#include "yggdrasil/formalism/columns_data.hpp"

#include <urd/elements/builder.hpp>
#include <urd/elements/children.hpp>
#include <urd/elements/flat.hpp>

namespace urd
{
/// The builder of Columns: its record's members.
template<typename C>
struct builder<ygg::Columns, C>
{
    urd::key<ygg::Columns> index;
    urd::built_t<urd::flat_vector<urd::data<ygg::ColumnLayout>>, C> values;

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"values", &builder::values>> {}; }
};

}  // namespace urd

#endif
