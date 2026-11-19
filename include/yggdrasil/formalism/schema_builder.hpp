/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_SCHEMA_BUILDER_HPP_
#define YGG_FORMALISM_SCHEMA_BUILDER_HPP_

#include "yggdrasil/formalism/schema_data.hpp"

#include <urd/elements/builder.hpp>
#include <urd/elements/children.hpp>
#include <urd/elements/flat.hpp>

namespace urd
{
/// The builder of Schema: its record's members.
template<typename C>
struct builder<ygg::Schema, C>
{
    urd::index<ygg::Schema> index;
    urd::built_t<urd::flat_vector<ygg::TypeId>, C> column_types;

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"column_types", &builder::column_types>> {}; }

    static constexpr auto identifying_members() noexcept { return urd::members_of(fields()); }
};

}  // namespace urd

#endif
