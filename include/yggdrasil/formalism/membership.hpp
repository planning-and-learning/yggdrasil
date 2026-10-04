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

#include "yggdrasil/core/types.hpp"

#include <concepts>
#include <memory>
#include <ranges>

namespace ygg::formalism
{
/// Check whether an indexed view denotes an entry in this repository or its ancestors.
template<typename Repository, typename T, typename C>
    requires requires(const Repository& repository, const C& context, Index<T> index) {
        { repository.contains(index) } -> std::same_as<bool>;
        { get_repository(context).contains(index) } -> std::same_as<bool>;
        repository.get_canonical_context(index);
        get_repository(context).get_canonical_context(index);
    }
bool contains(const Repository& repository, const ::ygg::View<Index<T>, C>& view)
{
    const auto index = view.get_index();
    const auto& source = get_repository(view.get_context());
    return repository.contains(index) && source.contains(index)
           && static_cast<const void*>(std::addressof(repository.get_canonical_context(index)))
                  == static_cast<const void*>(std::addressof(source.get_canonical_context(index)));
}

/// Check presence for indices, and presence plus canonical ownership for indexed views.
template<typename Repository, std::ranges::input_range Range>
    requires requires(const Repository& repository, std::ranges::range_reference_t<Range> value) {
        { repository.contains(value) } -> std::same_as<bool>;
    }
bool contains_all(const Repository& repository, Range&& values)
{
    for (auto&& value : values)
        if (!repository.contains(value))
            return false;
    return true;
}
}  // namespace ygg::formalism

#endif
