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

#ifndef YGG_FORMALISM_ROW_DATA_HPP_
#define YGG_FORMALISM_ROW_DATA_HPP_

#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/values.hpp"

#include <string_view>
#include <urd/elements/flat.hpp>
#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>
#include <utility>

namespace urd
{

/// A row held outside the repository (a builder of one, or a borrowed row): its pool's key and its canonical values, one
/// per column (an object as its index's value).
template<typename F>
struct data<ygg::Row<F>>
{
    urd::index<F> relation;
    urd::flat_vector<ygg::Value> values;

    data() = default;
    data(urd::index<F> relation_, urd::flat_vector<ygg::Value> values_) : relation(relation_), values(std::move(values_)) {}
    data(const data& other) = delete;
    data& operator=(const data& other) = delete;
    data(data&& other) = default;
    data& operator=(data&& other) = default;
    ~data() = default;

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"relation", &data::relation>, urd::field<"values", &data::values>> {}; }
    static constexpr auto identifying_members() noexcept { return urd::members_of(fields()); }
    static constexpr auto cista_members() noexcept { return urd::Members<&data::relation, &data::values> {}; }
};

}  // namespace urd

namespace ygg
{
/// A stored row as its context hands it out (context[key]): its relation, which its key names, and its values, decoded
/// from its pool (Values: the context's packed row). The row itself is stored packed, its relation the pool; this value
/// has the fields of data<Row<F>>, so that a stored row's view reads it as any record's (urd's member<>).
template<typename F, typename Values>
struct StoredRowRecord
{
    static constexpr std::string_view name = "StoredRowRecord";

    urd::index<F> relation;
    Values values;

    static constexpr auto fields() noexcept
    { return urd::Fields<urd::field<"relation", &StoredRowRecord::relation>, urd::field<"values", &StoredRowRecord::values>> {}; }
    static constexpr auto identifying_members() noexcept { return urd::members_of(fields()); }
};
}  // namespace ygg

#endif
