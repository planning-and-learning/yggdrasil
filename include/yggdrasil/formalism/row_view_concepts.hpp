/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_ROW_VIEW_CONCEPTS_HPP_
#define YGG_FORMALISM_ROW_VIEW_CONCEPTS_HPP_

#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/values.hpp"

#include <concepts>
#include <cstddef>
#include <type_traits>
#include <urd/elements/view.hpp>

namespace ygg
{
/// Whether V is the view of a row of family F (a stored row, one held outside the repository, a builder's: its tag is
/// Row<F>): its values, read untyped, through its context. Read through const, however V is spelled (a reference to a
/// mutable view too). It asks only for members whose types are written out: get_relation() and get_values() deduce
/// theirs, and asking for them would compile their reads through the repository wherever the concept is checked; the
/// tag gives a view both.
template<typename V, typename F>
concept RowViewConcept = std::same_as<typename std::remove_cvref_t<V>::value_type, Row<F>> && requires(const std::remove_reference_t<V>& view, std::size_t i) {
    view.context();
    { view.value(i) } -> std::same_as<Value>;
};

/// Whether a row read through view V resolves its columns of record type T (RowView's resolve<T>): V's context resolves
/// indices of T (urd::resolves), as a formalism repository does its objects; a free relation's repository resolves none.
template<typename V, typename T>
concept ResolvesColumns = urd::resolves<urd::view_context_t<V>, T>;

/// Whether a context C resolves objects (urd::resolves): a row read through it gives its object columns as objects
/// (RowView's resolve<Object>, resolve_object and resolve_objects in Python).
template<typename C>
concept ResolvesObjects = urd::resolves<C, formalism::Object>;

}  // namespace ygg

#endif
