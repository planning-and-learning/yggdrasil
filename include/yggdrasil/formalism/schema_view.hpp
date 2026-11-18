/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_SCHEMA_VIEW_HPP_
#define YGG_FORMALISM_SCHEMA_VIEW_HPP_

#include "yggdrasil/formalism/schema_data.hpp"

#include <urd/elements/view.hpp>

/// A Schema's view: its column types.
template<typename V>
struct urd::view_mixin<ygg::Schema, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] decltype(auto) get_types() const noexcept { return self().template member<&urd::record_of<V>::types>(); }
};

#endif
