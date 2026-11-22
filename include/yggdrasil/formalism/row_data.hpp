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

#include <cstddef>
#include <urd/elements/flat.hpp>
#include <urd/elements/hash.hpp>
#include <urd/elements/packed_row.hpp>
#include <urd/repr/containers/schema_rows.hpp>
#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>
#include <utility>

namespace ygg
{

/// A row of family F as its pool's slot holds it: its values packed at the slot's column widths, each column read on
/// demand, atomically (so that one reader serves concurrent and sequential slots alike), and the relation of its slot,
/// which the slot keeps once and hands to each row it hands out. What a lookup of a row hands out, by value
/// (urd::stored_ref<Row<F>>); its slot must outlive it. A random-access range of Values.
template<typename F>
class PackedRow
{
    urd::packed_row<true> m_row;
    urd::index<F> m_relation;

public:
    using value_type = Value;
    using const_iterator = typename urd::packed_row<true>::const_iterator;
    using iterator = const_iterator;
    using size_type = std::size_t;

    /// The row a slot of `relation` hands out (urd::packed_row of either thread safety), read from where it lies.
    template<bool ThreadSafe>
    PackedRow(const urd::packed_row<ThreadSafe>& row, urd::index<F> relation) noexcept :
        m_row(row.data(), row.first_bit(), row.column_widths()),
        m_relation(relation)
    {
    }

    /// The relation of its slot.
    [[nodiscard]] urd::index<F> relation() const noexcept { return m_relation; }

    [[nodiscard]] size_type size() const noexcept { return m_row.size(); }

    [[nodiscard]] bool empty() const noexcept { return m_row.empty(); }

    /// The value of a column.
    [[nodiscard]] Value operator[](size_type column) const noexcept { return m_row.get(column); }

    [[nodiscard]] const_iterator begin() const noexcept { return m_row.begin(); }

    [[nodiscard]] const_iterator end() const noexcept { return m_row.end(); }

    /// The widths its slot packs its columns at.
    [[nodiscard]] const urd::column_widths& column_widths() const noexcept { return m_row.column_widths(); }
};

}  // namespace ygg

namespace urd
{

/// A row owned outside its pool: its relation (the pool it is stored in: its key's first part) and its values, one per
/// column (an object as its index's value), unpacked. Identified by its values alone (its content: what its slot packs,
/// PackedRow<F>, and what an insertion or a lookup deduplicates in the slot its relation selects: urd::insert_at,
/// urd::find_at, the slot taking its values): hashed and compared as its packed form is.
template<typename F>
struct data<ygg::Row<F>>
{
    urd::index<F> relation;
    urd::flat_vector<ygg::Value> values;
    /// Its id in its relation's slot: its key's last part, written by the slot at insertion (a record holds its whole
    /// key; the slot keeps neither: its relation is the slot's, its id its place).
    urd::index<ygg::Row<F>> index;

    data() = default;

    data(urd::index<F> relation_, urd::flat_vector<ygg::Value> values_) : relation(relation_), values(std::move(values_)) {}

    data(const data& other) = delete;
    data& operator=(const data& other) = delete;
    data(data&& other) = default;
    data& operator=(data&& other) = default;
    ~data() = default;

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"relation", &data::relation>, urd::field<"values", &data::values>> {}; }

    /// Its content alone: the relation selects the slot, the values are what the slot deduplicates.
    static constexpr auto identifying_members() noexcept { return urd::Members<&data::values> {}; }

    static constexpr auto cista_members() noexcept { return urd::Members<&data::relation, &data::values, &data::index> {}; }
};

/// A row's content hashes as the range of its values, owned (data<Row<F>>) or packed in its slot (PackedRow<F>) alike,
/// as its slot hashes it (urd::hash_range): equal content, equal hash, in either form.
template<typename F>
struct Hash<data<ygg::Row<F>>>
{
    [[nodiscard]] hash_t operator()(const data<ygg::Row<F>>& row) const { return urd::hash_range(row.values); }
};

template<typename F>
struct Hash<ygg::PackedRow<F>>
{
    [[nodiscard]] hash_t operator()(const ygg::PackedRow<F>& row) const { return urd::hash_range(row); }
};

/// A stored row's fields read through its packed form: its values the packed row itself, its relation its slot's.
template<typename F, urd::flat_vector<ygg::Value> data<ygg::Row<F>>::* Member>
struct stored_field<Member, ygg::PackedRow<F>>
{
    [[nodiscard]] static ygg::PackedRow<F> read(const ygg::PackedRow<F>& row) noexcept { return row; }
};

template<typename F, urd::index<F> data<ygg::Row<F>>::* Member>
struct stored_field<Member, ygg::PackedRow<F>>
{
    [[nodiscard]] static urd::index<F> read(const ygg::PackedRow<F>& row) noexcept { return row.relation(); }
};

}  // namespace urd

#endif
