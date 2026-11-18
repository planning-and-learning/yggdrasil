/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_SCHEMA_DATA_HPP_
#define YGG_FORMALISM_SCHEMA_DATA_HPP_

#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/values.hpp"

#include <urd/elements/flat.hpp>
#include <utility>

namespace urd
{
/// Column types: a pool's (every pool has one), and the key of free relations' pools (labels belong to their Columns).
template<>
struct data<ygg::Schema>
{
    urd::index<ygg::Schema> index;
    urd::flat_vector<ygg::TypeId> types;

    data() = default;

    explicit data(urd::flat_vector<ygg::TypeId> types_) : index(), types(std::move(types_)) {}

    data(const data& other) = delete;
    data& operator=(const data& other) = delete;
    data(data&& other) = default;
    data& operator=(data&& other) = default;
    ~data() = default;

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"types", &data::types>> {}; }

    static constexpr auto identifying_members() noexcept { return urd::members_of(fields()); }

    static constexpr auto cista_members() noexcept { return urd::Members<&data::index, &data::types> {}; }
};

}  // namespace urd

#endif
