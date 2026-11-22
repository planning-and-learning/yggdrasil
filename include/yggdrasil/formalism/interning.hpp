/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_INTERNING_HPP_
#define YGG_FORMALISM_INTERNING_HPP_

#include "yggdrasil/formalism/builder.hpp"
#include "yggdrasil/formalism/canonicalization.hpp"
#include "yggdrasil/formalism/column_layout.hpp"
#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/relation_data.hpp"

#include <algorithm>
#include <boost/mp11.hpp>
#include <cassert>
#include <concepts>
#include <optional>
#include <span>
#include <stdexcept>
#include <urd/elements/builder.hpp>
#include <urd/elements/children.hpp>
#include <urd/elements/flat.hpp>
#include <urd/elements/view.hpp>
#include <utility>

namespace ygg
{
/// A relation record made canonical and checked before it is inserted (by the repository's one insert): its rows in tuple order,
/// unique; its columns' types its pool's (its relation symbol's: std::out_of_range where no layer holds it), their
/// labels distinct (std::invalid_argument otherwise).
template<typename Repository, typename F>
    requires formalism::SupportsSymbol<Repository, Relation<F>>
void prepare_for_insert(Repository& repository, urd::data<Relation<F>>& relation)
{
    ygg::canonicalize(relation, repository);
    const auto& values = repository[relation.columns]->values;
    const auto columns = std::span<const urd::data<ColumnLayout>>(values.data(), values.size());
    if (!std::ranges::equal(repository.template types<F>(relation.pool), columns, {}, {}, &urd::data<ColumnLayout>::type))
        throw std::invalid_argument("Repository: the columns' types are not the pool's.");
    for (auto it = columns.begin(); it != columns.end(); ++it)
        if (repeats_label(columns, it))
            throw std::invalid_argument("Repository: a relation's column labels are distinct.");
    // ponytail: an assert, not a throw: O(rows) per insert (user). Catches ids beyond the pool (e.g. of another
    // repository), not an id that happens to exist in both.
    assert(std::ranges::all_of(relation.rows,
                               [&](urd::key<F, Row<F>> key) { return urd::get<F>(key) == relation.pool && repository.owner(key) != nullptr; })
           && "Repository: a relation's rows must be rows of its pool here.");
}

// Columns, a Schema and a row are canonical as they are written (their order is their meaning): inserted as they are,
// by the no-op default (declarations.hpp).
}  // namespace ygg

namespace ygg::formalism
{

/// Whether a repository interns a data<T>: its one insert (KeyedRepository: preparing it first, where its domain does,
/// prepare_for_insert), of a kind it holds or a repository nested in it holds.
template<typename Repository, typename T>
concept InsertsRecord = requires(Repository& repository, urd::data<T>& data) { repository.insert(data); };

/// Whether a repository finds a data<T>: its one find, likewise.
template<typename Repository, typename T>
concept FindsRecord = requires(const Repository& repository, const urd::data<T>& data) { repository.find(data); };

}  // namespace ygg::formalism

namespace ygg::formalism
{
/// What a repository's find gives for a record of T: the optional of its view there, under its key.
template<typename Repository, typename T>
using found_t = std::optional<urd::index_view<T, Repository, key_t<T>>>;

/// Whether a repository interns a builder of another repository's kind, which it holds nested (its insert(builder,
/// storage) passing it on: an execution repository's planning state, say).
template<typename C, typename Storage, typename T, typename Context>
concept InsertsNestedBuilder =
    !std::same_as<Context, C> && requires(C& repository, Storage& storage, const urd::builder<T, Context>& built) { repository.insert(built, storage); };

/// Likewise for finding (find(builder, storage)).
template<typename C, typename Storage, typename T, typename Context>
concept FindsNestedBuilder =
    !std::same_as<Context, C> && requires(C& repository, Storage& storage, const urd::builder<T, Context>& built) { repository.find(built, storage); };

namespace detail
{
/// Whether a child's form is its key (an interned child: an index, or a key of its own such as a row's, a value), not a
/// builder. A record not interned yet is a key's sibling form too, but it comes writable (data<T>&) and is taken by its
/// own overloads, which a const key does not reach.
template<typename K>
concept ChildKey = !urd::is_builder<K>;

/// The forms of a child interning a builder meets, each inserted into `repository`, with scratch records from
/// `storage`, a builder storage of the repository (scratch<T>()); declared first, as they call one another.
template<typename C, IsBuilderStorage Storage, ChildKey K>
K insert_child(C& repository, Storage& storage, const K& interned);
template<typename C, IsBuilderStorage Storage, typename T>
    requires InsertsRecord<C, T>
auto insert_child(C& repository, Storage& storage, urd::data<T>& record);
template<typename C, IsBuilderStorage Storage, typename T>
    requires(!InsertsRecord<C, T>)
[[noreturn]] urd::missing_key insert_child(C& repository, Storage& storage, urd::data<T>& record);
template<typename C, IsBuilderStorage Storage, typename T>
decltype(auto) insert_child(C& repository, Storage& storage, const urd::builder<T, C>& built);
template<typename C, IsBuilderStorage Storage, typename T, typename Context>
    requires InsertsNestedBuilder<C, Storage, T, Context>
decltype(auto) insert_child(C& repository, Storage& storage, const urd::builder<T, Context>& built);
template<typename C, IsBuilderStorage Storage, typename T, typename Context>
    requires(!std::same_as<Context, C> && !InsertsNestedBuilder<C, Storage, T, Context>)
[[noreturn]] urd::missing_key insert_child(C& repository, Storage& storage, const urd::builder<T, Context>& built);

/// An interned child: its key, as it is (of this repository's kind, or of another's: a match-tree node's formalism atom).
/// A key names no repository: that it is one of the repository it is interned into is its writer's to keep.
template<typename C, IsBuilderStorage Storage, ChildKey K>
K insert_child(C& /*repository*/, Storage& /*storage*/, const K& interned)
{ return interned; }

/// A record not interned yet: inserted by the repository's one insert (prepared there, as its domain makes it
/// canonical), into the repository that holds its kind, this one or one nested in it.
template<typename C, IsBuilderStorage Storage, typename T>
    requires InsertsRecord<C, T>
auto insert_child(C& repository, Storage& /*storage*/, urd::data<T>& record)
{ return repository.insert(record); }

/// A record of a kind no repository reached through this one holds: a child of another repository, which must be
/// interned there first (std::invalid_argument).
template<typename C, IsBuilderStorage Storage, typename T>
    requires(!InsertsRecord<C, T>)
[[noreturn]] urd::missing_key insert_child(C& /*repository*/, Storage& /*storage*/, urd::data<T>& /*record*/)
{ throw std::invalid_argument("ygg::formalism: a child of another repository is interned in its own repository first"); }

/// A builder: its children first, then its record, written into a scratch record of `storage`, prepared and inserted
/// (as a record not interned yet is: under the parts of its key it holds, where its key has several).
template<typename C, IsBuilderStorage Storage, typename T>
decltype(auto) insert_child(C& repository, Storage& storage, const urd::builder<T, C>& built)
{
    auto scratch = storage.template scratch<T>();
    return urd::intern(built, *scratch, [&repository, &storage](auto& form) -> decltype(auto) { return detail::insert_child(repository, storage, form); });
}

/// A builder of a kind a repository nested in this one holds: interned there, through this one.
template<typename C, IsBuilderStorage Storage, typename T, typename Context>
    requires InsertsNestedBuilder<C, Storage, T, Context>
decltype(auto) insert_child(C& repository, Storage& storage, const urd::builder<T, Context>& built)
{ return repository.insert(built, storage); }

/// A builder of another repository's record, which no repository reached through this one holds: likewise
/// (std::invalid_argument).
template<typename C, IsBuilderStorage Storage, typename T, typename Context>
    requires(!std::same_as<Context, C> && !InsertsNestedBuilder<C, Storage, T, Context>)
[[noreturn]] urd::missing_key insert_child(C& /*repository*/, Storage& /*storage*/, const urd::builder<T, Context>& /*built*/)
{ throw std::invalid_argument("ygg::formalism: a child of another repository is interned in its own repository first"); }

/// The forms of a child finding a builder meets, each looked up in `repository` and nothing inserted: what the lookup
/// gives (a key, the optional of a view, or missing_key for a child no record here can name); declared first.
template<typename C, IsBuilderStorage Storage, ChildKey K>
K find_child(C& repository, Storage& storage, const K& interned);
template<typename C, IsBuilderStorage Storage, typename T>
    requires FindsRecord<C, T>
auto find_child(C& repository, Storage& storage, urd::data<T>& record);
template<typename C, IsBuilderStorage Storage, typename T>
    requires(!FindsRecord<C, T>)
urd::missing_key find_child(C& repository, Storage& storage, urd::data<T>& record);
template<typename C, IsBuilderStorage Storage, typename T>
found_t<C, T> find_child(C& repository, Storage& storage, const urd::builder<T, C>& built);
template<typename C, IsBuilderStorage Storage, typename T, typename Context>
    requires FindsNestedBuilder<C, Storage, T, Context>
auto find_child(C& repository, Storage& storage, const urd::builder<T, Context>& built);
template<typename C, IsBuilderStorage Storage, typename T, typename Context>
    requires(!std::same_as<Context, C> && !FindsNestedBuilder<C, Storage, T, Context>)
urd::missing_key find_child(C& repository, Storage& storage, const urd::builder<T, Context>& built);

/// An interned child: its key, as insert_child gives it.
template<typename C, IsBuilderStorage Storage, ChildKey K>
K find_child(C& /*repository*/, Storage& /*storage*/, const K& interned)
{ return interned; }

/// A record not interned yet: prepared in place, where its domain does (its canonical form: the same record), and looked
/// up by the repository's one find, in the repository that holds its kind, this one or one nested in it.
template<typename C, IsBuilderStorage Storage, typename T>
    requires FindsRecord<C, T>
auto find_child(C& repository, Storage& /*storage*/, urd::data<T>& record)
{
    prepare_for_insert(repository, record);
    return repository.find(std::as_const(record));
}

/// A builder of a kind a repository nested in this one holds: looked up there, through this one.
template<typename C, IsBuilderStorage Storage, typename T, typename Context>
    requires FindsNestedBuilder<C, Storage, T, Context>
auto find_child(C& repository, Storage& storage, const urd::builder<T, Context>& built)
{ return repository.find(built, storage); }

/// A record or a builder of another repository's kind, which no repository reached through this one holds: not
/// interned there, so no record here names it.
template<typename C, IsBuilderStorage Storage, typename T>
    requires(!FindsRecord<C, T>)
urd::missing_key find_child(C& /*repository*/, Storage& /*storage*/, urd::data<T>& /*record*/)
{ return {}; }

template<typename C, IsBuilderStorage Storage, typename T, typename Context>
    requires(!std::same_as<Context, C> && !FindsNestedBuilder<C, Storage, T, Context>)
urd::missing_key find_child(C& /*repository*/, Storage& /*storage*/, const urd::builder<T, Context>& /*built*/)
{ return {}; }

/// Whether a child's lookup found nothing: missing_key, or an empty lookup result; a key is found.
inline bool is_missing(const urd::missing_key& /*result*/) noexcept { return true; }

template<typename V>
bool is_missing(const std::optional<V>& result) noexcept
{ return !result.has_value(); }

template<typename K>
bool is_missing(const K& /*key*/) noexcept
{ return false; }

/// A builder: its children looked up first (a child not found: its record is not found either), then its record,
/// written into a scratch record of `storage`, prepared and looked up (as a record not interned yet is).
template<typename C, IsBuilderStorage Storage, typename T>
found_t<C, T> find_child(C& repository, Storage& storage, const urd::builder<T, C>& built)
{
    auto scratch = storage.template scratch<T>();
    auto found = true;
    urd::map_children(built,
                      *scratch,
                      [&repository, &storage, &found](auto& child) -> decltype(auto)
                      {
                          auto key = detail::find_child(repository, storage, child);
                          if (detail::is_missing(key))
                              found = false;
                          return key;
                      });
    if (!found)
        return found_t<C, T> {};
    return detail::find_child(repository, storage, *scratch);
}

/// The builder a view holds, interned as a built child. (A view of a record of its own is not interned this way: its
/// record is not written, and records are not copied; such a record is interned as a child, where it is held.)
template<typename C, IsBuilderStorage Storage, typename T>
decltype(auto) insert_record(C& repository, Storage& storage, const urd::builder<T, C>& built)
{ return detail::insert_child(repository, storage, built); }

/// Likewise for finding.
template<typename C, IsBuilderStorage Storage, typename T>
found_t<C, T> find_record(C& repository, Storage& storage, const urd::builder<T, C>& built)
{ return detail::find_child(repository, storage, built); }

/// A view of a record of this repository's kind: an interned one (its own: std::invalid_argument where another
/// repository of the kind holds it, as its key would name another record here), or one holding a builder (interned as
/// a built child is).
template<typename C, typename T, typename K>
void check_own(const C& repository, const urd::index_view<T, C, K>& interned)
{
    if (repository.view(interned.index()) != interned)
        throw std::invalid_argument("ygg::formalism: a view of another repository");
}

template<typename C, IsBuilderStorage Storage, typename T, typename K>
std::pair<urd::index_view<T, C, K>, bool> insert_view(C& repository, Storage& /*storage*/, const urd::index_view<T, C, K>& interned)
{
    detail::check_own(repository, interned);
    return { interned, false };
}

template<typename C, IsBuilderStorage Storage, urd::View V>
decltype(auto) insert_view(C& repository, Storage& storage, const V& view)
{ return detail::insert_record(repository, storage, urd::detail::view_access::record(view)); }

template<typename C, IsBuilderStorage Storage, typename T, typename K>
std::optional<urd::index_view<T, C, K>> find_view(C& repository, Storage& /*storage*/, const urd::index_view<T, C, K>& interned)
{
    detail::check_own(repository, interned);
    return interned;
}

template<typename C, IsBuilderStorage Storage, urd::View V>
auto find_view(C& repository, Storage& storage, const V& view)
{ return detail::find_record(repository, storage, urd::detail::view_access::record(view)); }
}  // namespace detail

/// Whether V is a view read through C (in any form: interned, or holding a builder or a record): what a child of C's
/// kinds resolves to, and what a repository interns as it interns that child.
template<typename V, typename C>
concept ViewThrough = urd::View<V> && std::same_as<std::remove_cvref_t<decltype(std::declval<const V&>().context())>, C>;

}  // namespace ygg::formalism

#endif
