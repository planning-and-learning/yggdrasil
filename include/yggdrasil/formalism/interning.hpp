/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_INTERNING_HPP_
#define YGG_FORMALISM_INTERNING_HPP_

#include "yggdrasil/formalism/builder.hpp"
#include "yggdrasil/formalism/canonicalization.hpp"
#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/relation_data.hpp"

#include <concepts>
#include <stdexcept>
#include <urd/elements/builder.hpp>
#include <urd/elements/children.hpp>
#include <urd/elements/flat.hpp>
#include <urd/elements/view.hpp>
#include <optional>
#include <utility>

namespace ygg
{
/// A relation record made canonical before it is inserted (ygg::formalism::insert): its rows in tuple order, unique.
template<typename Repository, typename F>
    requires formalism::SupportsSymbol<Repository, Relation<F>>
void prepare_for_insert(Repository& repository, urd::data<Relation<F>>& relation)
{ ygg::canonicalize(relation, repository); }

/// Columns, a Schema and a row are canonical as they are written (their order is their meaning): inserted as they are.
template<typename Repository>
void prepare_for_insert(Repository& /*repository*/, urd::data<Columns>& /*columns*/) noexcept
{
}

template<typename Repository>
void prepare_for_insert(Repository& /*repository*/, urd::data<Schema>& /*schema*/) noexcept
{
}

template<typename Repository, typename F>
void prepare_for_insert(Repository& /*repository*/, urd::data<Row<F>>& /*row*/) noexcept
{
}
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

namespace ygg::formalism
{
/// What a repository's find gives for a record of T: the optional of its view there, under its key.
template<typename Repository, typename T>
using found_t = std::optional<urd::index_view<T, Repository, key_t<T>>>;

/// Whether a repository interns a record of another repository's kind, which it holds nested (its insert(record)
/// passing it on to that repository: an execution repository's registers, say).
template<typename C, typename T>
concept InsertsNestedRecord = !PreparedInsert<C, T> && requires(C& repository, urd::data<T>& record) { repository.insert(record); };

/// Whether a repository interns a builder of another repository's kind, which it holds nested (its insert(builder,
/// storage) passing it on: an execution repository's planning state, say).
template<typename C, typename Storage, typename T, typename Context>
concept InsertsNestedBuilder =
    !std::same_as<Context, C> && requires(C& repository, Storage& storage, const urd::builder<T, Context>& built) { repository.insert(built, storage); };

/// Likewise for finding (find(record), find(builder, storage)).
template<typename C, typename T>
concept FindsNestedRecord = !PreparedInsert<C, T> && requires(C& repository, const urd::data<T>& record) { repository.find(record); };

template<typename C, typename Storage, typename T, typename Context>
concept FindsNestedBuilder =
    !std::same_as<Context, C> && requires(C& repository, Storage& storage, const urd::builder<T, Context>& built) { repository.find(built, storage); };

namespace detail
{
/// The forms of a child interning a builder meets, each inserted into `repository`, with scratch records from
/// `storage`, a builder storage of the repository (scratch<T>()); declared first, as they call one another.
template<typename C, IsBuilderStorage Storage, typename T, typename K>
K insert_child(C& repository, Storage& storage, const urd::index_view<T, C, K>& interned);
template<typename C, IsBuilderStorage Storage, typename T, typename Context, typename K>
    requires(!std::same_as<Context, C>)
K insert_child(C& repository, Storage& storage, const urd::index_view<T, Context, K>& interned);
template<typename C, IsBuilderStorage Storage, typename T>
    requires PreparedInsert<C, T>
auto insert_child(C& repository, Storage& storage, urd::data<T>& record);
template<typename C, IsBuilderStorage Storage, typename T>
    requires InsertsNestedRecord<C, T>
auto insert_child(C& repository, Storage& storage, urd::data<T>& record);
template<typename C, IsBuilderStorage Storage, typename T>
    requires(!PreparedInsert<C, T> && !InsertsNestedRecord<C, T>)
[[noreturn]] urd::missing_key insert_child(C& repository, Storage& storage, urd::data<T>& record);
template<typename C, IsBuilderStorage Storage, typename T>
decltype(auto) insert_child(C& repository, Storage& storage, const urd::builder<T, C>& built);
template<typename C, IsBuilderStorage Storage, typename T, typename Context>
    requires InsertsNestedBuilder<C, Storage, T, Context>
decltype(auto) insert_child(C& repository, Storage& storage, const urd::builder<T, Context>& built);
template<typename C, IsBuilderStorage Storage, typename T, typename Context>
    requires(!std::same_as<Context, C> && !InsertsNestedBuilder<C, Storage, T, Context>)
[[noreturn]] urd::missing_key insert_child(C& repository, Storage& storage, const urd::builder<T, Context>& built);

/// An interned child of this repository's kind: its key, where the repository holds it (this one or a parent layer);
/// std::invalid_argument where another repository of the kind holds it (its key names another record here).
template<typename C, IsBuilderStorage Storage, typename T, typename K>
K insert_child(C& repository, Storage& /*storage*/, const urd::index_view<T, C, K>& interned)
{
    if (repository.view(interned.index()) != interned)
        throw std::invalid_argument("ygg::formalism: a child interned in another repository");
    return interned.index();
}

/// An interned child of another kind of repository (a match-tree node's formalism atom): its key.
template<typename C, IsBuilderStorage Storage, typename T, typename Context, typename K>
    requires(!std::same_as<Context, C>)
K insert_child(C& /*repository*/, Storage& /*storage*/, const urd::index_view<T, Context, K>& interned)
{ return interned.index(); }

/// A record not interned yet: prepared (its domain makes it canonical) and inserted.
template<typename C, IsBuilderStorage Storage, typename T>
    requires PreparedInsert<C, T>
auto insert_child(C& repository, Storage& /*storage*/, urd::data<T>& record)
{ return formalism::insert(repository, record); }

/// A record of a kind a repository nested in this one holds: interned there, through this one.
template<typename C, IsBuilderStorage Storage, typename T>
    requires InsertsNestedRecord<C, T>
auto insert_child(C& repository, Storage& /*storage*/, urd::data<T>& record)
{ return repository.insert(record); }

/// A record of a kind no repository reached through this one holds: a child of another repository, which must be
/// interned there first (std::invalid_argument).
template<typename C, IsBuilderStorage Storage, typename T>
    requires(!PreparedInsert<C, T> && !InsertsNestedRecord<C, T>)
[[noreturn]] urd::missing_key insert_child(C& /*repository*/, Storage& /*storage*/, urd::data<T>& /*record*/)
{ throw std::invalid_argument("ygg::formalism: a child of another repository is interned in its own repository first"); }

/// A builder: its children first, then its record, written into a scratch record of `storage`, prepared and inserted.
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
template<typename C, IsBuilderStorage Storage, typename T, typename K>
K find_child(C& repository, Storage& storage, const urd::index_view<T, C, K>& interned);
template<typename C, IsBuilderStorage Storage, typename T, typename Context, typename K>
    requires(!std::same_as<Context, C>)
K find_child(C& repository, Storage& storage, const urd::index_view<T, Context, K>& interned);
template<typename C, IsBuilderStorage Storage, typename T>
    requires PreparedInsert<C, T>
found_t<C, T> find_child(C& repository, Storage& storage, urd::data<T>& record);
template<typename C, IsBuilderStorage Storage, typename T>
    requires FindsNestedRecord<C, T>
auto find_child(C& repository, Storage& storage, urd::data<T>& record);
template<typename C, IsBuilderStorage Storage, typename T>
    requires(!PreparedInsert<C, T> && !FindsNestedRecord<C, T>)
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
template<typename C, IsBuilderStorage Storage, typename T, typename K>
K find_child(C& repository, Storage& storage, const urd::index_view<T, C, K>& interned)
{ return detail::insert_child(repository, storage, interned); }

template<typename C, IsBuilderStorage Storage, typename T, typename Context, typename K>
    requires(!std::same_as<Context, C>)
K find_child(C& /*repository*/, Storage& /*storage*/, const urd::index_view<T, Context, K>& interned)
{ return interned.index(); }

/// A record not interned yet: prepared in place (its canonical form: the same record) and looked up.
template<typename C, IsBuilderStorage Storage, typename T>
    requires PreparedInsert<C, T>
found_t<C, T> find_child(C& repository, Storage& /*storage*/, urd::data<T>& record)
{
    prepare_for_insert(repository, record);
    return repository.find(std::as_const(record));
}

/// A record or a builder of a kind a repository nested in this one holds: looked up there, through this one.
template<typename C, IsBuilderStorage Storage, typename T>
    requires FindsNestedRecord<C, T>
auto find_child(C& repository, Storage& /*storage*/, urd::data<T>& record)
{ return repository.find(std::as_const(record)); }

template<typename C, IsBuilderStorage Storage, typename T, typename Context>
    requires FindsNestedBuilder<C, Storage, T, Context>
auto find_child(C& repository, Storage& storage, const urd::builder<T, Context>& built)
{ return repository.find(built, storage); }

/// A record or a builder of another repository's kind, which no repository reached through this one holds: not
/// interned there, so no record here names it.
template<typename C, IsBuilderStorage Storage, typename T>
    requires(!PreparedInsert<C, T> && !FindsNestedRecord<C, T>)
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
/// written into a scratch record of `storage`, prepared and looked up.
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
    prepare_for_insert(repository, *scratch);
    return repository.find(std::as_const(*scratch));
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

/// A view of a record of this repository's kind: an interned one (its own, checked as an interned child is), or one
/// holding a builder (interned as a built child is).
template<typename C, IsBuilderStorage Storage, typename T, typename K>
std::pair<urd::index_view<T, C, K>, bool> insert_view(C& repository, Storage& storage, const urd::index_view<T, C, K>& interned)
{ return { repository.view(detail::insert_child(repository, storage, interned)), false }; }

template<typename C, IsBuilderStorage Storage, urd::View V>
decltype(auto) insert_view(C& repository, Storage& storage, const V& view)
{ return detail::insert_record(repository, storage, urd::detail::view_access::record(view)); }

template<typename C, IsBuilderStorage Storage, typename T, typename K>
std::optional<urd::index_view<T, C, K>> find_view(C& repository, Storage& storage, const urd::index_view<T, C, K>& interned)
{ return repository.view(detail::find_child(repository, storage, interned)); }

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
