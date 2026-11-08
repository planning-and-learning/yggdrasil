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

#include <urd/elements/flat.hpp>
#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>
#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/values.hpp"

#include <utility>

namespace ygg
{
/// The type of family F's values: an object index where F's rows are objects (F::object_tag names them), a Value for
/// Schema.
template<typename F>
struct row_value
{
    using type = urd::index<formalism::Object<typename F::object_tag>>;
};
template<>
struct row_value<Schema>
{
    using type = Value;
};
template<typename F>
using row_value_t = typename row_value<F>::type;

}  // namespace ygg

namespace urd
{

/// A row held outside the repository (a builder of one, or a borrowed row): its pool's key and its values.
template<typename F>
struct data<ygg::Row<F>>
{
    urd::index<F> relation;
    urd::flat_vector<ygg::row_value_t<F>> values;

    data() = default;
    data(urd::index<F> relation_, urd::flat_vector<ygg::row_value_t<F>> values_) : relation(relation_), values(std::move(values_)) {}
    data(const data& other) = delete;
    data& operator=(const data& other) = delete;
    data(data&& other) = default;
    data& operator=(data&& other) = default;

    static constexpr auto cista_members() noexcept { return urd::Members<&data::relation, &data::values> {}; }
    static constexpr auto identifying_members() noexcept { return urd::Members<&data::relation, &data::values> {}; }
};

}  // namespace urd

#endif
