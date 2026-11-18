/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_COLUMNS_VIEW_HPP_
#define YGG_FORMALISM_COLUMNS_VIEW_HPP_

#include "yggdrasil/formalism/columns_data.hpp"

#include <urd/elements/view.hpp>

/// A Columns' view: its columns, labeled and typed.
template<typename V>
struct urd::view_mixin<ygg::Columns, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] decltype(auto) get_values() const noexcept { return self().template member<&urd::record_of<V>::values>(); }
};

#endif
