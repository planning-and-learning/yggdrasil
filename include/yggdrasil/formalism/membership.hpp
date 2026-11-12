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

#include <urd/elements/view.hpp>

namespace ygg::formalism
{
/// Whether `key` names the same record in `repository` (or an ancestor) as in `source`: both find it, in the same layer.
template<typename Repository, typename Source, typename Key>
bool same_owner(const Repository& repository, const Source& source, const Key& key)
{
    const auto* owner = repository.owner(key);
    return owner != nullptr && static_cast<const void*>(owner) == static_cast<const void*>(source.owner(key));
}

/// Whether a view of an index denotes an entry in this repository or its ancestors.
template<typename Repository, typename T, typename C>
    requires requires(const Repository& repository, urd::index<T> index) { repository.owner(index); }
bool contains(const Repository& repository, const urd::index_view<T, C>& view)
{ return same_owner(repository, get_repository(view.context()), view.index()); }

/// Whether a view of a row key denotes a row in this repository or its ancestors.
template<typename Repository, typename B, typename C, typename Key>
    requires requires(const Repository& repository, const Key& key) { repository.owner(key); }
bool contains(const Repository& repository, const urd::owning_view<B, C, Key>& view)
{ return same_owner(repository, get_repository(view.context()), view.data()); }

}  // namespace ygg::formalism

#endif
