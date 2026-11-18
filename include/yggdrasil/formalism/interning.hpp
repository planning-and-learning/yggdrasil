/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_INTERNING_HPP_
#define YGG_FORMALISM_INTERNING_HPP_

#include "yggdrasil/formalism/canonicalization.hpp"
#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/relation_data.hpp"

#include <concepts>
#include <stdexcept>
#include <urd/elements/builder.hpp>
#include <urd/elements/children.hpp>
#include <urd/elements/flat.hpp>
#include <urd/elements/view.hpp>
#include <utility>

namespace ygg
{
/// A relation record made canonical before it is inserted (ygg::formalism::insert): its rows in tuple order, unique.
template<typename Repository, typename F>
    requires formalism::SupportsSymbol<Repository, Relation<F>>
void prepare_for_insert(Repository& repository, urd::data<Relation<F>>& relation)
{ ygg::canonicalize(relation, repository); }
}  // namespace ygg

namespace ygg::formalism
{

/// Whether a repository interns a data<T> once its domain has prepared it (prepare_for_insert, found by ADL): a symbol
/// or a row.
template<typename Repository, typename T>
concept PreparedInsert = requires(Repository& repository, urd::data<T>& data) {
    prepare_for_insert(repository, data);
    repository.insert(data);
};

/// Prepares data through its domain's hook (prepare_for_insert: canonicalizes it), then interns it in the repository.
template<typename Repository, typename T>
    requires PreparedInsert<Repository, T>
[[nodiscard]] auto insert(Repository& repository, urd::data<T>& data)
{
    prepare_for_insert(repository, data);
    return repository.insert(data);
}

}  // namespace ygg::formalism

namespace ygg
{
/// The forms of a child interning a builder meets, each inserted into `repository` (urd::intern's insert, by
/// insert_child): an interned child as its key, where the repository holds it (std::invalid_argument where it is
/// another repository's); a record as the repository inserts it, as given (a builder's record is canonical: its builder
/// keeps it so); a builder interned again, with a scratch record of its own.
template<typename C, typename T, typename K>
K insert_child(C& repository, const urd::index_view<T, C, K>& interned)
{
    if (repository.view(interned.index()) != interned)
        throw std::invalid_argument("ygg::intern: a child interned in another repository.");
    return interned.index();
}

template<typename C, typename T>
decltype(auto) insert_child(C& repository, urd::data<T>& record)
{ return repository.insert(record); }

template<typename C, typename T>
decltype(auto) insert_child(C& repository, const urd::builder<T, C>& built)
{
    auto scratch = urd::data<T>();  // ponytail: a scratch per builder; pooled where interning shows in profiles
    return urd::intern(built, scratch, [&repository](auto& child) -> decltype(auto) { return ygg::insert_child(repository, child); });
}

/// Interns a builder into `repository`, its built children first: the view of its record there, and whether it was new.
template<typename T, typename C>
decltype(auto) intern(const urd::builder<T, C>& built, C& repository)
{ return ygg::insert_child(repository, built); }
}  // namespace ygg

#endif
