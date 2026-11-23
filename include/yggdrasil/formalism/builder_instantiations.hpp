/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_FORMALISM_BUILDER_INSTANTIATIONS_HPP_
#define YGG_FORMALISM_BUILDER_INSTANTIATIONS_HPP_

// Explicit instantiations of a BuilderStorage (builder.hpp) for each of its types (rule 7 of the template rules): the one
// source of a library that includes builder_impl.hpp instantiates each of its storages,
//
//     YGG_FORMALISM_BUILDER(, Builder, Count)
//
// and its other sources include builder.hpp only and link to these.

#include "yggdrasil/formalism/builder_impl.hpp"

#include <boost/mp11.hpp>
#include <boost/preprocessor/repetition/repeat.hpp>
#include <boost/preprocessor/repetition/repeat_from_to.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <cstddef>

namespace ygg::formalism::detail
{
/// The I-th type of a builder storage.
template<typename Builder, std::size_t I>
using BuilderType = boost::mp11::mp_at_c<typename Builder::Types, I>;
}  // namespace ygg::formalism::detail

/// The members of the I-th type of the storage (ARGS: (PREFIX, Builder)).
// NOLINTBEGIN(cppcoreguidelines-macro-usage,bugprone-macro-parentheses): the macros expand to explicit instantiations,
// declarations, which no function gives; their arguments are types and a prefix, which no parentheses may enclose
#define YGG_FORMALISM_BUILDER_AT(z, I, ARGS)                                                                                                        \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                 \
    template urd::handle<                                                                                                                           \
        urd::builder<::ygg::formalism::detail::BuilderType<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>, typename BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::Context>> \
        BOOST_PP_TUPLE_ELEM(2, 1, ARGS)::do_checkout<::ygg::formalism::detail::BuilderType<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>();                  \
    BOOST_PP_TUPLE_ELEM(2, 0, ARGS)                                                                                                                 \
    template urd::handle<urd::data<::ygg::formalism::detail::BuilderType<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>> BOOST_PP_TUPLE_ELEM(                 \
        2,                                                                                                                                          \
        1,                                                                                                                                          \
        ARGS)::do_scratch<::ygg::formalism::detail::BuilderType<BOOST_PP_TUPLE_ELEM(2, 1, ARGS), I>>();

/// Explicit instantiations (PREFIX extern: declarations) of builder storage Builder (a name without commas: an alias):
/// construction, destruction, and its members for each of its Count types. The count is checked against its list.
#define YGG_FORMALISM_BUILDER(PREFIX, Builder, Count)                                                      \
    static_assert(boost::mp11::mp_size<Builder::Types>::value == (Count), "YGG_FORMALISM_BUILDER: Count"); \
    YGG_FORMALISM_BUILDER_MEMBERS(PREFIX, Builder)                                                         \
    BOOST_PP_REPEAT(Count, YGG_FORMALISM_BUILDER_AT, (PREFIX, Builder))

/// The same in parts, for a storage of many types split over several sources (brokkr's template rule T5): its
/// construction and destruction, and its members for its types First..Last-1.
#define YGG_FORMALISM_BUILDER_MEMBERS(PREFIX, Builder) \
    PREFIX template Builder::BasicBuilderStorage();    \
    PREFIX template Builder::~BasicBuilderStorage();
#define YGG_FORMALISM_BUILDER_TYPES(PREFIX, Builder, First, Last) BOOST_PP_REPEAT_FROM_TO(First, Last, YGG_FORMALISM_BUILDER_AT, (PREFIX, Builder))

// NOLINTEND(cppcoreguidelines-macro-usage,bugprone-macro-parentheses)

#endif
