/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_INTERNING_HPP_
#define YGG_FORMALISM_INTERNING_HPP_

#include "yggdrasil/core/types.hpp"

#include <concepts>
#include <utility>

namespace ygg::formalism
{

/// Prepare data through its domain's ADL hook, then intern it in the
/// repository.
template<typename Repository, typename T>
    requires requires(Repository& repository, Data<T>& data) {
        { prepare_for_interning(repository, data) } -> std::same_as<void>;
        { repository.get_or_create(data) } -> std::same_as<std::pair<View<Index<T>, Repository>, bool>>;
    }
[[nodiscard]] auto get_or_create(Repository& repository, Data<T>& data)
{
    prepare_for_interning(repository, data);
    return repository.get_or_create(data);
}

}  // namespace ygg::formalism

#endif
