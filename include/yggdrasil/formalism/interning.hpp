/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_INTERNING_HPP_
#define YGG_FORMALISM_INTERNING_HPP_

#include <urd/elements/flat.hpp>

#include <concepts>
#include <utility>

namespace ygg::formalism
{

/// Prepare data through its domain's ADL hook, then intern it in the
/// repository.
template<typename Repository, typename T>
    requires requires(Repository& repository, urd::data<T>& data) {
        { prepare_for_insert(repository, data) } -> std::same_as<void>;
        { repository.insert(data).first.context() } -> std::same_as<const Repository&>;
        { repository.insert(data).second } -> std::convertible_to<bool>;
    }
[[nodiscard]] auto insert(Repository& repository, urd::data<T>& data)
{
    prepare_for_insert(repository, data);
    return repository.insert(data);
}

}  // namespace ygg::formalism

#endif
