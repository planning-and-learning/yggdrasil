/*
 * Copyright (C) 2025-2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_FORMALISM_MEMBERSHIP_HPP_
#define YGG_FORMALISM_MEMBERSHIP_HPP_


#include <concepts>
#include <memory>
#include <ranges>

namespace ygg::formalism
{
/// Check whether an indexed view (of any record's handle) denotes an entry in this repository or its ancestors.
template<typename Repository, typename V>
    requires requires(const Repository& repository, const V& view, decltype(view.index()) index) {
        { repository.contains(index) } -> std::same_as<bool>;
        { get_repository(view.context()).contains(index) } -> std::same_as<bool>;
        repository.get_canonical_context(index);
        get_repository(view.context()).get_canonical_context(index);
    }
bool contains(const Repository& repository, const V& view)
{
    const auto index = view.index();
    const auto& source = get_repository(view.context());
    return repository.contains(index) && source.contains(index)
           && static_cast<const void*>(std::addressof(repository.get_canonical_context(index)))
                  == static_cast<const void*>(std::addressof(source.get_canonical_context(index)));
}

}  // namespace ygg::formalism

#endif
