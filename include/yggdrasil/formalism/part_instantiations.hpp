/*
 * Copyright (C) 2025-2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef YGG_FORMALISM_PART_INSTANTIATIONS_HPP_
#define YGG_FORMALISM_PART_INSTANTIATIONS_HPP_

// Explicit instantiations of the members of a repository's own part for each key it resolves (rule 7 of the template
// rules, https://planning-and-learning.github.io/brokkr/template_rules.html), shared by the repositories
// (repository_instantiations.hpp) and the symbol contexts (symbol_context_instantiations.hpp): for the P-th of Owner's
// own parts (Owner::OwnParts<Writable>), each of its Count keys (its KeyTypes) read, looked for, owned, viewed, counted,
// found and stored.

#include "yggdrasil/formalism/keyed.hpp"

#include <boost/mp11.hpp>
#include <boost/preprocessor/repetition/repeat.hpp>
#include <boost/preprocessor/repetition/repeat_from_to.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <cstddef>
#include <optional>
#include <tuple>
#include <utility>

namespace ygg::formalism::detail
{
/// The P-th own part of Owner, as read (Writable false) or written into.
template<typename Owner, std::size_t P, bool Writable>
using InstantiatedPart = std::tuple_element_t<P, typename Owner::template OwnParts<Writable>>;
/// Its I-th key, the kind the key names, and the parts of the key a record of the kind holds before it is stored.
template<typename Owner, std::size_t P, std::size_t I>
using PartKey = boost::mp11::mp_at_c<typename InstantiatedPart<Owner, P, false>::KeyTypes, I>;
template<typename Owner, std::size_t P, std::size_t I>
using PartKind = kind_of_key<PartKey<Owner, P, I>>;
template<typename Owner, std::size_t P, std::size_t I>
using PartKnown = boost::mp11::mp_rename<boost::mp11::mp_pop_back<boost::mp11::mp_rename<PartKey<Owner, P, I>, boost::mp11::mp_list>>, urd::key>;
}  // namespace ygg::formalism::detail

// NOLINTBEGIN(cppcoreguidelines-macro-usage,bugprone-macro-parentheses): the macros expand to explicit instantiations,
// declarations, which no function gives; their arguments are types and a prefix, which no parentheses may enclose
#define YGG_FORMALISM_PART_PREFIX(ARGS) BOOST_PP_TUPLE_ELEM(3, 0, ARGS)
#define YGG_FORMALISM_PART_OWNER(ARGS) BOOST_PP_TUPLE_ELEM(3, 1, ARGS)
#define YGG_FORMALISM_PART_READ(ARGS) ygg::formalism::detail::InstantiatedPart<YGG_FORMALISM_PART_OWNER(ARGS), BOOST_PP_TUPLE_ELEM(3, 2, ARGS), false>
#define YGG_FORMALISM_PART_WRITE(ARGS) ygg::formalism::detail::InstantiatedPart<YGG_FORMALISM_PART_OWNER(ARGS), BOOST_PP_TUPLE_ELEM(3, 2, ARGS), true>
#define YGG_FORMALISM_PART_KEY_T(I, ARGS) ::ygg::formalism::detail::PartKey<YGG_FORMALISM_PART_OWNER(ARGS), BOOST_PP_TUPLE_ELEM(3, 2, ARGS), I>
#define YGG_FORMALISM_PART_KIND_T(I, ARGS) ::ygg::formalism::detail::PartKind<YGG_FORMALISM_PART_OWNER(ARGS), BOOST_PP_TUPLE_ELEM(3, 2, ARGS), I>
#define YGG_FORMALISM_PART_KNOWN_T(I, ARGS) ::ygg::formalism::detail::PartKnown<YGG_FORMALISM_PART_OWNER(ARGS), BOOST_PP_TUPLE_ELEM(3, 2, ARGS), I>

/// The members of the part for its I-th key (ARGS: (PREFIX, Owner, P)).
#define YGG_FORMALISM_PART_KEY(z, I, ARGS)                                                                                                           \
    YGG_FORMALISM_PART_PREFIX(ARGS)                                                                                                                  \
    template urd::stored_ref<YGG_FORMALISM_PART_KIND_T(I, ARGS)> YGG_FORMALISM_PART_READ(ARGS)::operator[](YGG_FORMALISM_PART_KEY_T(I, ARGS)) const; \
    YGG_FORMALISM_PART_PREFIX(ARGS)                                                                                                                  \
    template urd::stored_ref<YGG_FORMALISM_PART_KIND_T(I, ARGS)> YGG_FORMALISM_PART_READ(ARGS)::at(YGG_FORMALISM_PART_KEY_T(I, ARGS)) const;         \
    YGG_FORMALISM_PART_PREFIX(ARGS) template bool YGG_FORMALISM_PART_READ(ARGS)::contains(YGG_FORMALISM_PART_KEY_T(I, ARGS)) const;                  \
    YGG_FORMALISM_PART_PREFIX(ARGS) template bool YGG_FORMALISM_PART_READ(ARGS)::owns(YGG_FORMALISM_PART_KEY_T(I, ARGS)) const;                      \
    YGG_FORMALISM_PART_PREFIX(ARGS)                                                                                                                  \
    template const YGG_FORMALISM_PART_OWNER(ARGS) * YGG_FORMALISM_PART_READ(ARGS)::owner(YGG_FORMALISM_PART_KEY_T(I, ARGS)) const;                   \
    YGG_FORMALISM_PART_PREFIX(ARGS)                                                                                                                  \
    template std::optional<urd::index_view<YGG_FORMALISM_PART_KIND_T(I, ARGS), YGG_FORMALISM_PART_OWNER(ARGS), YGG_FORMALISM_PART_KEY_T(I, ARGS)>>   \
        YGG_FORMALISM_PART_READ(ARGS)::find(urd::data<YGG_FORMALISM_PART_KIND_T(I, ARGS)>&) const;                                                   \
    YGG_FORMALISM_PART_PREFIX(ARGS)                                                                                                                  \
    template std::pair<urd::index_view<YGG_FORMALISM_PART_KIND_T(I, ARGS), YGG_FORMALISM_PART_OWNER(ARGS), YGG_FORMALISM_PART_KEY_T(I, ARGS)>, bool> \
        YGG_FORMALISM_PART_WRITE(ARGS)::insert(urd::data<YGG_FORMALISM_PART_KIND_T(I, ARGS)>&) const;                                                \
    YGG_FORMALISM_PART_PREFIX(ARGS)                                                                                                                  \
    template std::size_t YGG_FORMALISM_PART_READ(ARGS)::size<YGG_FORMALISM_PART_KIND_T(I, ARGS)>(YGG_FORMALISM_PART_KNOWN_T(I, ARGS)) const;         \
    YGG_FORMALISM_PART_PREFIX(ARGS)                                                                                                                  \
    template std::size_t YGG_FORMALISM_PART_READ(ARGS)::own_size<YGG_FORMALISM_PART_KIND_T(I, ARGS)>(::urd::key<>) const;                            \
    YGG_FORMALISM_PART_PREFIX(ARGS)                                                                                                                  \
    template std::size_t YGG_FORMALISM_PART_READ(ARGS)::memory_usage<YGG_FORMALISM_PART_KIND_T(I, ARGS)>(::urd::key<>) const;

/// Explicit instantiations (PREFIX extern: declarations) of the members of the P-th own part of Owner (a name without
/// commas: an alias) for each of its Count keys. The count is checked against its KeyTypes.
#define YGG_FORMALISM_PART(PREFIX, Owner, P, Count)                                                                                                            \
    static_assert(boost::mp11::mp_size<::ygg::formalism::detail::InstantiatedPart<Owner, P, false>::KeyTypes>::value == (Count), "YGG_FORMALISM_PART: Count"); \
    BOOST_PP_REPEAT(Count, YGG_FORMALISM_PART_KEY, (PREFIX, Owner, P))

/// The same for its keys First..Last-1 only: a part of many keys split over several sources, each of which
/// instantiates a range (brokkr's template rule T5), so that no one source holds them all.
#define YGG_FORMALISM_PART_KEYS(PREFIX, Owner, P, First, Last) BOOST_PP_REPEAT_FROM_TO(First, Last, YGG_FORMALISM_PART_KEY, (PREFIX, Owner, P))

// NOLINTEND(cppcoreguidelines-macro-usage,bugprone-macro-parentheses)

#endif
