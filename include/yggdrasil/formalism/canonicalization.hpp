/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_CANONICALIZATION_HPP_
#define YGG_FORMALISM_CANONICALIZATION_HPP_

#include "yggdrasil/formalism/columns_data.hpp"
#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/relation_data.hpp"
#include "yggdrasil/formalism/schema_data.hpp"
#include "yggdrasil/formalism/values.hpp"
#include "yggdrasil/semantics/canonicalization.hpp"

#include <algorithm>
#include <ranges>
#include <urd/elements/data.hpp>
#include <urd/elements/view.hpp>
#include <utility>
#include <variant>

namespace ygg
{

/**
 * Canonicalization of ygg's records (found by ADL). Every record has a canonicalize overload, a no-op where it is
 * canonical as built, so a record without one is a compile error, and is_canonical, whether it is. A row is its family's (its tag's library's).
 */

/// f of a row's values: a row read as the view it is (a row child resolved, an interned row, a row builder's or record's
/// view), or bare values.
template<RowValues R, typename F>
decltype(auto) with_values(const R& values, F&& f)
{ return std::forward<F>(f)(values); }

template<urd::View V, typename F>
decltype(auto) with_values(const V& row, F&& f)
{ return std::forward<F>(f)(row.get_values()); }

template<urd::View... Vs, typename F>
decltype(auto) with_values(const std::variant<Vs...>& row, F&& f)
{ return urd::visit_view(row, [&f](const auto& view) -> decltype(auto) { return std::forward<F>(f)(view.get_values()); }); }

/// The order of rows: by their values, lexicographically (tuple order), whatever their keys or forms, each read through
/// its own context. A relation's rows are in this order, its builder's set of row children by it. Transparent: it
/// compares a row with bare values too, so that a set of rows is searched by values.
struct TupleOrder
{
    using is_transparent = void;

    template<typename L, typename R>
    bool operator()(const L& lhs, const R& rhs) const
    {
        return ygg::with_values(lhs,
                                [&rhs](const auto& left)
                                { return ygg::with_values(rhs, [&left](const auto& right) { return std::ranges::lexicographical_compare(left, right); }); });
    }
};

inline void canonicalize(urd::data<Schema>&)
{
    // Canonical as built: the order of the types is the columns'
}

inline bool is_canonical(const urd::data<Schema>&) { return true; }

inline void canonicalize(urd::data<Columns>&)
{
    // Canonical as built: the order of the columns is the rows'
}

inline bool is_canonical(const urd::data<Columns>&) { return true; }

/// A relation's canonical form: its rows in tuple order (their values, read through `repository`, lexicographically) and
/// unique (equal values in one pool are one key), as a builder keeps them.
template<typename F, typename Repository>
    requires formalism::SupportsSymbol<Repository, Relation<F>>
void canonicalize(urd::data<Relation<F>>& relation, const Repository& repository)
{
    std::ranges::sort(relation.rows, std::ranges::lexicographical_compare, [&repository](RowIndex<F> key) { return repository.values(key); });
    relation.rows.erase(std::ranges::unique(relation.rows).begin(), relation.rows.end());
}

/// Whether a relation is canonical: its rows in tuple order and unique.
template<typename F, typename Repository>
    requires formalism::SupportsSymbol<Repository, Relation<F>>
bool is_canonical(const urd::data<Relation<F>>& relation, const Repository& repository)
{
    return std::ranges::is_sorted(relation.rows, std::ranges::lexicographical_compare, [&repository](RowIndex<F> key) { return repository.values(key); })
           && std::ranges::adjacent_find(relation.rows) == relation.rows.end();
}

}  // namespace ygg

#endif
