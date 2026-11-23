/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_ROW_BUILDER_HPP_
#define YGG_FORMALISM_ROW_BUILDER_HPP_

#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/row_data.hpp"
#include "yggdrasil/formalism/values.hpp"

#include <urd/elements/builder.hpp>
#include <urd/elements/children.hpp>
#include <urd/elements/flat.hpp>

namespace urd
{
/// The builder of a row of family F: its record's members, its id in its relation's slot (its key's last part) and its
/// relation (its pool's key) a child.
template<typename F, typename C>
struct builder<ygg::Row<F>, C>
{
    urd::key<ygg::Row<F>> index;
    urd::built_t<urd::index<F>, C> relation;
    urd::built_t<urd::flat_vector<ygg::Value>, C> values;

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"relation", &builder::relation>, urd::field<"values", &builder::values>> {}; }

    static constexpr auto identifying_members() noexcept { return urd::members_of(fields()); }
};
}  // namespace urd

#endif
