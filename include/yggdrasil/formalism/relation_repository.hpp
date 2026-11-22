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

#ifndef YGG_FORMALISM_RELATION_REPOSITORY_HPP_
#define YGG_FORMALISM_RELATION_REPOSITORY_HPP_

#include "yggdrasil/core/config.hpp"
#include "yggdrasil/core/family.hpp"
#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/relation_data.hpp"
#include "yggdrasil/formalism/row_data.hpp"
#include "yggdrasil/formalism/symbol_repository.hpp"
#include "yggdrasil/formalism/values.hpp"

#include <boost/mp11.hpp>
#include <concepts>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <urd/elements/flat.hpp>
#include <urd/memory.hpp>
#include <urd/repr/maps/index_map.hpp>
#include <urd/repr/pools/pools.hpp>
#include <urd/repr/wrappers/layer.hpp>
#include <urd/repr/wrappers/layered_pool.hpp>
#include <urd/store.hpp>
#include <urd/tags.hpp>
#include <utility>

namespace ygg::formalism
{

/// The width of a whole object index, the widest a relation repository stores.
inline constexpr auto default_object_index_width = urd::bit_width { static_cast<std::uint8_t>(std::numeric_limits<ygg::uint_t>::digits) };

/// How a relation repository stores its rows' objects (Width): rows of one unsigned column per argument
/// (urd::by_column_widths), each object column 32 bits (urd::word), or the object_index_width bits the largest index
/// needs (urd::bit).
template<typename Width>
concept ObjectWidth = std::same_as<Width, urd::word> || std::same_as<Width, urd::bit>;

template<typename Width, typename... Ts>
class RelationRepository;

template<typename Width, typename... Ts>
class ConcurrentRelationRepository;

namespace detail
{
/// A relation's slot, as the fold reaches it (urd::record_at, urd::find_at, urd::insert_at): its pool of rows (Rows:
/// const where read) and the relation, kept once and handed to each row it hands out (PackedRow<F>). A handle: the pool
/// must outlive it.
template<typename F, typename Rows>
class RowSlot
{
    Rows* m_rows;
    urd::index<F> m_relation;

public:
    RowSlot(Rows& rows, urd::index<F> relation) noexcept : m_rows(&rows), m_relation(relation) {}

    /// The row at `at`, packed, with its slot's relation.
    [[nodiscard]] PackedRow<F> operator[](urd::index<Row<F>> at) const { return PackedRow<F>((*m_rows)[at], m_relation); }

    /// The number of rows along the layers.
    [[nodiscard]] std::size_t size() const { return m_rows->size(); }

    /// The id of the row of this content (its values), in whichever layer holds it, or none.
    [[nodiscard]] urd::index<Row<F>> find(const urd::data<Row<F>>& content) const { return m_rows->find(content.values); }

    /// The id of the row of this content (its values) stored where no layer holds it, and whether it was stored now.
    /// The slot keeps its values alone (its relation the slot's, its id its place): `make` is not called.
    template<typename Make>
    std::pair<urd::index<Row<F>>, bool> insert(const urd::data<Row<F>>& content, Make /*make*/) const
        requires(!std::is_const_v<Rows>)
    { return m_rows->insert(content.values); }
};

/// The rows of a repository layer, for each family F (predicates, actions, ..., Schema).
///
/// Rows live in pools. A pool holds each row once and numbers it (its id, an index<Row<F>>), and has a key (an
/// index<F>, its relation symbol) whose record stores its column types (column_types). For example:
///
///   - predicate `at` (key: index<Predicate> 3, column types (object, object)): pool of rows (truck1, loc2), ...;
///   - free relations of two uint columns (key: their Schema S, index<Schema> 1, column types (uint, uint)): pool of
///     rows (1, 2), (3, 4), ....
///
/// A layer makes a pool on its first row (own()), its widths those of its key's column types. A relation (a symbol
/// record: pool key, columns, row ids) is a set of row ids in one pool; many relations share a pool. Every Schema is
/// interned once in the layer's Schema pool. Over a parent, everything layers: a pool this layer never wrote reads the
/// parent's; one it wrote to has a pool of its own over the parent's, its row ids continuing after the parent's.
template<typename Width, bool ThreadSafe, typename... Ts>
    requires ObjectWidth<Width>
class RelationRepositoryBase
{
    using Threading = std::conditional_t<ThreadSafe, urd::concurrent, urd::single_threaded>;

public:
    using width = Width;
    using RelationTypes = boost::mp11::mp_list<Ts...>;
    /// The types of the records it holds (get<T>()): the Schemas, its rows' column types.
    using RecordTypes = boost::mp11::mp_list<Schema>;
    static constexpr bool thread_safe = ThreadSafe;

    /// One pool of rows: each row once, numbered in insertion order. A row is a Value per column, stored in its column's
    /// bits (e.g. (truck1, loc2) as two object indices of object_bits() each). Emptied by clear.
    template<typename F>
    using Rows = urd::Store<urd::pool<urd::Store<urd::sequence<Value>, urd::frozen>, urd::index<Row<F>>>,
                            urd::monotone_until_clear,
                            urd::by_column_widths,
                            urd::layered,
                            Threading>;
    /// Family F's pools of rows, by key: e.g. predicate 3 (`at`) -> the pool of its rows (truck1, loc2), (truck2, loc1);
    /// for free relations, Schema (object, object) -> the pool of every free relation's rows of those types. Clear keeps
    /// each pool's storage (constant_clear), which the pool made again for that key reuses.
    template<typename F>
    using Pools = urd::Store<urd::map<urd::index<F>, Rows<F>>, urd::dense, urd::layered, urd::constant_clear, Threading>;
    /// Every Schema, each once, as a flat record: e.g. (object, object) -> index<Schema> 0, (uint, uint) -> 1. Kept
    /// across clear, so the keys of free relations' pools stay valid.
    using Schemas =
        urd::Store<urd::pool<urd::data<Schema>, urd::index<Schema>>, urd::flat, urd::stable_refs, urd::monotone_until_clear, urd::layered, Threading>;

private:
    Family<Pools, Ts...> m_relations;
    Schemas m_schemas;
    std::uint8_t m_object_index_width;

public:
    /// The pools of family F, as this layer reads them.
    template<typename F>
        requires HasRows<RelationRepositoryBase, F>
    [[nodiscard]] const Pools<F>& pools() const
    { return m_relations.template get<F>(); }

    /// The pool `relation`, as this layer reads it (its own, or the nearest layer's below): the step a row's key takes
    /// to its slot (urd::SelectsBy: urd::record_at, urd::find_at); std::out_of_range where no layer has it.
    template<typename F>
        requires HasRows<RelationRepositoryBase, F>
    [[nodiscard]] RowSlot<F, const Rows<F>> at(urd::index<F> relation) const
    { return { pools<F>().at(relation), relation }; }

    /// f of the pool `relation`, as this layer reads it, else `missing`.
    template<typename F, typename Function, typename Result>
        requires HasRows<RelationRepositoryBase, F>
    [[nodiscard]] Result visit(urd::index<F> relation, Function f, Result missing) const
    {
        return pools<F>().visit(relation, [relation, &f](const Rows<F>& rows) { return f(RowSlot<F, const Rows<F>>(rows, relation)); }, missing);
    }

    /// This layer's own pool `relation`, made where it has none, with the widths of its column types `types` (its
    /// relation symbol's, stored in its record), over the pool of the nearest layer below that has one (its row ids
    /// continuing after that one's): the step an insertion takes to the slot (a repository's writable relations:
    /// urd::insert_at).
    template<typename F>
        requires HasRows<RelationRepositoryBase, F>
    RowSlot<F, Rows<F>> own(urd::index<F> relation, std::span<const TypeId> types)
    {
        auto& relations = m_relations.template get<F>();
        if (auto* rows = relations.own_value(relation))
            return { *rows, relation };
        relations.try_emplace(relation, widths(types));
        return { *relations.own_value(relation), relation };
    }

    /// The number of rows of the pool `relation`, along the layers.
    template<typename F>
        requires HasRows<RelationRepositoryBase, F>
    [[nodiscard]] size_t size(urd::index<F> relation) const
    {
        return pools<F>().visit(relation, [](const Rows<F>& rows) { return size_t(rows.size()); }, size_t { 0 });
    }

    /// The Schema `record`, interned here where no layer holds it (record.index set to it).
    urd::index<Schema> schema(urd::data<Schema>& record) { return intern(m_schemas, record).first; }

    /// The store of T's records, one of RecordTypes (the step a key of a record takes first: urd::SelectsStoreOf): the
    /// layered Store of the Schemas.
    template<typename T>
        requires boost::mp11::mp_contains<RecordTypes, T>::value
    [[nodiscard]] const Schemas& get() const noexcept
    { return m_schemas; }

    template<typename T>
        requires boost::mp11::mp_contains<RecordTypes, T>::value
    [[nodiscard]] Schemas& get() noexcept
    { return m_schemas; }


    /// The bits of an object column: the layer's object index width (urd::bit), else 32 (urd::word).
    [[nodiscard]] std::uint8_t object_bits() const noexcept
    {
        constexpr bool packed = std::same_as<Width, urd::bit>;
        return packed ? m_object_index_width : default_object_index_width.bits;
    }

    /// The widths of this layer's rows of these column types: each whole, an object column object_bits(). The one width
    /// computation of pools and of the layer's builders.
    template<std::ranges::input_range Types>
    [[nodiscard]] urd::column_widths widths(Types&& types) const
    {
        auto result = urd::column_widths();
        for (const TypeId type : std::forward<Types>(types))
            result.push_back(column_bits(type, object_bits()));
        return result;
    }

    /// Parent layers must remain frozen and outlive this repository.
    explicit RelationRepositoryBase(const RelationRepositoryBase* parent = nullptr, urd::bit_width object_index_width = default_object_index_width) :
        m_relations(parent == nullptr ? nullptr : &parent->m_relations),
        m_schemas(parent == nullptr ? nullptr : &parent->m_schemas),  // numbered after the parent's
        m_object_index_width(object_index_width.bits)
    {
        if (m_object_index_width == 0 || m_object_index_width > default_object_index_width.bits)
            throw std::invalid_argument("RelationRepository: object index width must be between 1 and the encoded object width.");
        if (parent && m_object_index_width < parent->m_object_index_width)
            throw std::invalid_argument("RelationRepository: child object index width must not be smaller than its parent's.");
    }

    ~RelationRepositoryBase() = default;
    RelationRepositoryBase(const RelationRepositoryBase&) = delete;
    RelationRepositoryBase& operator=(const RelationRepositoryBase&) = delete;
    RelationRepositoryBase(RelationRepositoryBase&&) = delete;
    RelationRepositoryBase& operator=(RelationRepositoryBase&&) = delete;

    [[nodiscard]] std::uint8_t get_object_index_width() const noexcept { return m_object_index_width; }

    /// This layer's own storage of F's rows.
    template<typename F>
        requires HasRows<RelationRepositoryBase, F>
    [[nodiscard]] size_t memory_usage() const
    { return urd::memory_usage(pools<F>()); }

    /// Forgets this layer's own rows; the parent's stay, and so do the Schemas and the pools' declarations (as the
    /// Repository keeps the pool keys).
    void clear() { m_relations.clear(); }
};
}  // namespace detail

template<typename Width, typename... Ts>
class RelationRepository : public detail::RelationRepositoryBase<Width, false, Ts...>
{
public:
    explicit RelationRepository(const RelationRepository* parent = nullptr, urd::bit_width object_index_width = default_object_index_width) :
        detail::RelationRepositoryBase<Width, false, Ts...>(parent, object_index_width)
    {
    }
};

template<typename Width, typename... Ts>
class ConcurrentRelationRepository : public detail::RelationRepositoryBase<Width, true, Ts...>
{
public:
    explicit ConcurrentRelationRepository(const ConcurrentRelationRepository* parent = nullptr,
                                          urd::bit_width object_index_width = default_object_index_width) :
        detail::RelationRepositoryBase<Width, true, Ts...>(parent, object_index_width)
    {
    }
};
}  // namespace ygg::formalism

#endif
