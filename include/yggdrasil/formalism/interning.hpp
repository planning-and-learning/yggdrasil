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
/// A Columns record checked before it is inserted (by the repository's one insert): its columns' types valid, their
/// labels distinct (validate_columns: std::invalid_argument otherwise).
template<typename Repository>
void prepare_for_insert(const Repository& /*repository*/, urd::data<Columns>& columns)
{ ygg::validate_columns(std::span<const urd::data<ColumnLayout>>(columns.values.data(), columns.values.size())); }

/// A relation record made canonical and checked before it is inserted (by the repository's one insert): its rows in tuple order,
/// unique; its columns' types its pool's (its relation symbol's: std::out_of_range where no layer holds it; its
/// columns were checked as they were inserted).
template<typename Repository, typename F>
    requires formalism::SupportsSymbol<Repository, Relation<F>>
void prepare_for_insert(const Repository& repository, urd::data<Relation<F>>& relation)
{
    ygg::canonicalize(relation, repository);
    const auto& values = repository[relation.layout]->values;
    const auto columns = std::span<const urd::data<ColumnLayout>>(values.data(), values.size());
    if (!std::ranges::equal(repository.view(relation.pool).column_types(), columns, {}, {}, &urd::data<ColumnLayout>::type))
        throw std::invalid_argument("Repository: the columns' types are not the pool's.");
    // ponytail: an assert, not a throw: O(rows) per insert (user). Catches ids beyond the pool (e.g. of another
    // repository), not an id that happens to exist in both.
    assert(std::ranges::all_of(relation.rows, [&](urd::key<F, Row<F>> key) { return urd::get<F>(key) == relation.pool && repository.owner(key) != nullptr; })
           && "Repository: a relation's rows must be rows of its pool here.");
}

// A Schema and a row are canonical as they are written (their order is their meaning): inserted as they are, by the
// default (declarations.hpp).
}  // namespace ygg

namespace ygg::formalism
{

/// Whether a repository interns a data<T>: its one insert (KeyedRepository: preparing it first, where its domain does,
/// prepare_for_insert), of a kind it holds or a repository nested in it holds.
template<typename Repository, typename T>
concept InsertsRecord = requires(Repository& repository, urd::data<T>& data) { repository.insert(data); };

/// Whether a repository finds a data<T>: its one find, likewise.
template<typename Repository, typename T>
concept FindsRecord = requires(const Repository& repository, urd::data<T>& data) { repository.find(data); };

/// Whether P (a repository, or a part of one) interns a builder B through a builder storage (insert(built, storage));
/// whether it finds the record B writes (find(built, storage)).
template<typename P, typename B, typename Storage>
concept InsertsBuilder = requires(P& held, const B& built, Storage& storage) { held.insert(built, storage); };

template<typename P, typename B, typename Storage>
concept FindsBuilder = requires(P& held, const B& built, Storage& storage) { held.find(built, storage); };

/// Whether P (a store, a part, a repository) interns a builder B itself, with the hints `Hints`
/// (insert_built(built, hints...)); finds the record it writes (find_built(built, hints...)); reads the record of a key
/// K into it (unpack_built(key, built)).
template<typename P, typename B, typename... Hints>
concept InsertsBuilt = requires(P& held, const B& built, const Hints&... hints) { held.insert_built(built, hints...); };

template<typename P, typename B, typename... Hints>
concept FindsBuilt = requires(const P& held, const B& built, const Hints&... hints) { held.find_built(built, hints...); };

template<typename P, typename K, typename B>
concept UnpacksBuilt = requires(const P& held, K key, B& built) { held.unpack_built(key, built); };

}  // namespace ygg::formalism

namespace ygg::formalism
{
/// What a repository's find gives for a record of T (KeyedRepository::find): the optional of its view through the
/// repository that owns its kind (Owner<T>: this one, or one nested in it), under its key.
template<typename Repository, typename T>
using found_t = std::optional<urd::index_view<T, typename Repository::template Owner<T>, key_t<T>>>;

namespace detail
{
/// The forms of a child interning or finding a builder meets: its key (an interned child: an index, or a key of its own
/// such as a row's, a value); a record not interned yet (urd::Record, writable); a builder (urd::Builder): one of
/// repository C itself, or one of a repository nested in C.
template<typename Form, typename C>
concept OwnBuilder = urd::Builder<Form> && std::same_as<urd::builder_context_t<Form>, C>;

template<typename Form, typename C>
concept NestedBuilder = urd::Builder<Form> && !std::same_as<urd::builder_context_t<Form>, C>;

/// Whether a child's form is a record repository C inserts (finds): of a kind it holds, or one nested in it holds.
template<typename Form, typename C>
concept InsertedRecord = urd::Record<Form> && InsertsRecord<C, urd::kind_of_t<Form>>;

template<typename Form, typename C>
concept FoundRecord = urd::Record<Form> && FindsRecord<C, urd::kind_of_t<Form>>;

/// Whether a child's lookup found nothing: missing_key, or an empty lookup result; a key is found.
template<typename R>
bool is_missing(const R& result) noexcept
{
    if constexpr (std::same_as<R, urd::missing_key>)
        return true;
    else if constexpr (urd::optional_value<R>)
        return !result.has_value();
    else
        return false;
}

/// A child of another repository, which no repository reached through this one holds: it is interned in its own
/// repository first (std::invalid_argument).
[[noreturn]] inline urd::missing_key other_repositorys_child()
{ throw std::invalid_argument("ygg::formalism: a child of another repository is interned in its own repository first"); }

/// A child inserted into `repository`, with scratch records from `storage` (a builder storage of the repository:
/// scratch<T>()), as interning a builder meets it: a key as it is (of this repository's kind, or of another's: a
/// match-tree node's formalism atom; that it is one of the repository it is interned into is its writer's to keep); a
/// record not interned yet by the repository's one insert (prepared there, as its domain makes it canonical), into the
/// repository that holds its kind; a builder of this repository, its children first, then its record, written into a
/// scratch record of `storage`, prepared and inserted (under the parts of its key it holds); a builder of a kind a
/// repository nested in this one holds, there, through this one. A record or builder no repository reached through
/// this one holds throws std::invalid_argument.
template<typename C, IsBuilderStorage Storage, typename Form, typename... Hints>
auto insert_child(C& repository, Storage& storage, Form& form, const Hints&... hints)
{
    using F = std::remove_const_t<Form>;
    if constexpr (OwnBuilder<F, C>)
    {
        // A builder its part interns itself (the part gives its store the hints the store takes); any other through a
        // scratch record, its children first, each with the hints.
        if constexpr (InsertsBuilt<C, F, Hints...>)
            return repository.insert_built(form, hints...);
        else
        {
            auto scratch = storage.template scratch<urd::kind_of_t<F>>();
            return urd::intern(form, *scratch, [&repository, &storage, &hints...](auto& child) { return detail::insert_child(repository, storage, child, hints...); });
        }
    }
    else if constexpr (NestedBuilder<F, C> && InsertsBuilder<C, F, Storage>)
        return repository.insert(form, storage);
    else if constexpr (InsertedRecord<F, C>)
        return repository.insert(form);
    else if constexpr (urd::Builder<F> || urd::Record<F>)
        return detail::other_repositorys_child();
    else
        return F(form);
}

/// A child looked up in `repository`, nothing inserted, as finding a builder meets it: what the lookup gives (a key, the
/// optional of a view, or missing_key for a child no record here can name). A builder its part finds itself
/// (find_built), with the hints the part takes; any other builder of this repository: its children looked up first (a
/// child not found: its record is not found either), each with the hints, then its record, written into a scratch record
/// of `storage`, prepared and looked up.
template<typename C, IsBuilderStorage Storage, typename Form, typename... Hints>
auto find_child(C& repository, Storage& storage, Form& form, const Hints&... hints)
{
    using F = std::remove_const_t<Form>;
    if constexpr (OwnBuilder<F, C> && FindsBuilt<C, F, Hints...>)
        return repository.find_built(form, hints...);
    else if constexpr (OwnBuilder<F, C>)
    {
        using T = urd::kind_of_t<F>;
        auto scratch = storage.template scratch<T>();
        auto found = true;
        urd::map_children(form,
                          *scratch,
                          [&repository, &storage, &found, &hints...](auto& child)
                          {
                              auto key = detail::find_child(repository, storage, child, hints...);
                              if (detail::is_missing(key))
                                  found = false;
                              return key;
                          });
        if (!found)
            return found_t<C, T> {};
        return found_t<C, T>(repository.find(*scratch));
    }
    else if constexpr (NestedBuilder<F, C> && FindsBuilder<C, F, Storage>)
        return repository.find(form, storage);
    else if constexpr (FoundRecord<F, C>)
        return repository.find(form);
    else if constexpr (urd::Builder<F> || urd::Record<F>)
        return urd::missing_key {};
    else
        return F(form);
}

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

/// The builder a view holds, interned as a built child (insert_child); find_view likewise finds it.
template<typename C, IsBuilderStorage Storage, urd::View V>
auto insert_view(C& repository, Storage& storage, const V& view)
{ return detail::insert_child(repository, storage, urd::record(view)); }

template<typename C, IsBuilderStorage Storage, typename T, typename K>
std::optional<urd::index_view<T, C, K>> find_view(C& repository, Storage& /*storage*/, const urd::index_view<T, C, K>& interned)
{
    detail::check_own(repository, interned);
    return interned;
}

template<typename C, IsBuilderStorage Storage, urd::View V>
auto find_view(C& repository, Storage& storage, const V& view)
{ return detail::find_child(repository, storage, urd::record(view)); }
}  // namespace detail

/// Whether V is a view read through C (in any form: interned, or holding a builder or a record): what a child of C's
/// kinds resolves to, and what a repository interns as it interns that child.
template<typename V, typename C>
concept ViewThrough = urd::View<V> && std::same_as<urd::view_context_t<V>, C>;

}  // namespace ygg::formalism

#endif
