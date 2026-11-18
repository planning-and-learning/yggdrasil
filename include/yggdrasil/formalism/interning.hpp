/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_INTERNING_HPP_
#define YGG_FORMALISM_INTERNING_HPP_

#include <concepts>
#include <stdexcept>
#include <urd/elements/builder.hpp>
#include <urd/elements/children.hpp>
#include <urd/elements/flat.hpp>
#include <urd/elements/view.hpp>
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

namespace ygg
{
/// Inserts each form of a child interning a builder meets into `repository` (urd::intern's insert): an interned child
/// as its key, where the repository holds it (std::invalid_argument where it is another repository's); a record as
/// the repository inserts it; a builder interned again, with a scratch record of its own.
template<typename C>
struct Inserter
{
    C* repository;

    template<typename T, typename K>
    K operator()(const urd::index_view<T, C, K>& interned) const
    {
        if (repository->view(interned.index()) != interned)
            throw std::invalid_argument("ygg::intern: a child interned in another repository.");
        return interned.index();
    }

    template<typename T>
    decltype(auto) operator()(urd::data<T>& record) const
    { return repository->insert(record); }

    template<typename T>
    decltype(auto) operator()(const urd::builder<T, C>& built) const
    {
        auto scratch = urd::data<T>();  // ponytail: a scratch per builder; pooled where interning shows in profiles
        return urd::intern(built, scratch, *this);
    }
};

/// Interns a builder into `repository`, its built children first: the view of its record there, and whether it was new.
template<typename T, typename C>
decltype(auto) intern(const urd::builder<T, C>& built, C& repository)
{ return Inserter<C> { &repository }(built); }
}  // namespace ygg

#endif
