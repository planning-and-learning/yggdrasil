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
#include <ranges>
#include <urd/elements/flat.hpp>
#include <urd/elements/hash.hpp>
#include <urd/elements/packed_row.hpp>
#include <urd/repr/containers/schema_rows.hpp>
#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>
#include <utility>

namespace ygg
{

/// A row of family F as its pool's slot holds it: urd's packed reader of its values (thread-safe: reading atomically,
/// it serves concurrent and sequential slots alike), and the relation of its slot, which the slot keeps once and hands
/// to each row it hands out. What a lookup of a row hands out, by value (urd::stored_ref<Row<F>>); its slot must outlive
/// it. A random-access view of Values: it owns none.
template<typename F>
class PackedRow : public urd::packed_row<true>, public std::ranges::view_base
{
    urd::index<F> m_relation;

public:
    /// The row a slot of `relation` hands out (urd::packed_row of either thread safety), read from where it lies.
    template<bool ThreadSafe>
    PackedRow(const urd::packed_row<ThreadSafe>& row, urd::index<F> relation) noexcept :
        urd::packed_row<true>(row.data(), row.first_bit(), row.column_widths()),
        m_relation(relation)
    {
    }

    /// The relation of its slot.
    [[nodiscard]] urd::index<F> relation() const noexcept { return m_relation; }
};

}  // namespace ygg

/// Its iterators are its packed row's, which read from the slot: they outlive it.
template<typename F>
inline constexpr bool std::ranges::enable_borrowed_range<ygg::PackedRow<F>> = true;

namespace urd
{

/// A row owned outside its pool: its relation (the pool it is stored in: its key's first part) and its values, one per
/// column (an object as its index's value), unpacked. Identified by its values alone (its content: what its slot packs,
/// PackedRow<F>, and what an insertion or a lookup deduplicates in the slot its relation selects: urd::insert_at,
/// urd::find_at, the slot taking its values): hashed and compared as its packed form is.
template<typename F>
struct data<ygg::Row<F>>
{
    /// Its number in its relation's slot: the local part of its key, written by the slot at insertion (key(): its whole
    /// key, its relation then this).
    urd::key<ygg::Row<F>> index;
    urd::index<F> relation;
    urd::flat_vector<ygg::Value> values;

    data() = default;

    data(urd::index<F> relation_, urd::flat_vector<ygg::Value> values_) : index(), relation(relation_), values(std::move(values_)) {}

    data(const data& other) = delete;
    data& operator=(const data& other) = delete;
    data(data&& other) = default;
    data& operator=(data&& other) = default;
    ~data() = default;

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"relation", &data::relation>, urd::field<"values", &data::values>> {}; }

    /// Its content alone: the relation selects the slot, the values are what the slot deduplicates.
    static constexpr auto identifying_members() noexcept { return urd::Members<&data::values> {}; }

    static constexpr auto cista_members() noexcept { return urd::cista_members_of<data>(); }

    /// Its whole key (urd::key_t<Row<F>>): the relation it lies in, then its number there.
    [[nodiscard]] urd::key<F, ygg::Row<F>> key() const noexcept { return urd::key<F, ygg::Row<F>>(relation, index); }
};

/// A packed row hashes as the range of its values, as an owned row does (urd hashes data<Row<F>> by its one identifying
/// member, its values, as that range): the slot finds a row by its owned form. urd's range hash is opt-in for the
/// ranges it knows, and the packed row is a reader urd does not know.
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
