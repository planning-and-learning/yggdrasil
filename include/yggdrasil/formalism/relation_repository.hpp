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
#include <utility>

namespace ygg::formalism
{

/// The width of a whole object index, the widest a relation repository stores.
inline constexpr auto default_object_index_width = urd::bit_width { static_cast<std::uint8_t>(std::numeric_limits<ygg::uint_t>::digits) };

/// How a relation repository stores its rows' objects, per object tag: rows of one unsigned column per argument
/// (urd::by_column_widths), each column 32 bits (urd::word), or the object_index_width bits the largest index needs
/// (urd::bit).
template<typename ObjectTag>
struct RelationRepositoryTraits
{
    using width = urd::word;
};

template<typename ObjectTag, typename... Ts>
class RelationRepository;

template<typename ObjectTag, typename... Ts>
class ConcurrentRelationRepository;

namespace detail
{
/// The rows of a repository layer, for each family F (predicates, actions, ..., Schema).
///
/// Rows live in pools. A pool holds each row once and numbers it (its id, an index<Row<F>>), and has a key (an
/// index<F>) and a Schema (its column types). For example:
///
///   - predicate `at` (key: index<Predicate> 3): pool of rows (truck1, loc2), (truck2, loc1), ...,
///     Schema (object, object);
///   - free relations of two uint columns (key: their Schema S, index<Schema> 1): pool of rows (1, 2), (3, 4), ...,
///     Schema S = (uint, uint).
///
/// A pool is declared with its Schema before its first row (declare): a key symbol (e.g. a predicate) when it is
/// interned, with the column types its view gives; free relations with their columns' types. Rows are then checked
/// against it. A relation (a symbol record: pool key, columns, row ids) is a set of row ids in one pool; many relations
/// share a pool. Every Schema is interned once in the layer's Schema pool, the one source of column types
/// (types(key)), and a pool's column widths come from it (widths()). Over a parent, everything layers: a pool this layer never wrote reads
/// the parent's; one it wrote to has a pool of its own over the parent's, its row ids continuing after the parent's.
template<typename ObjectTag, bool ThreadSafe, typename... Ts>
class RelationRepositoryBase
{
    using Threading = std::conditional_t<ThreadSafe, urd::concurrent, urd::single_threaded>;

public:
    using object_tag = ObjectTag;
    using RelationTypes = boost::mp11::mp_list<Ts...>;
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
    /// Family F's pools' Schemas, by key, as declared: e.g. predicate 3 -> (object, object). Kept across clear, as the
    /// pool keys and the Schemas are.
    template<typename F>
    using PoolSchemas = urd::Store<urd::map<urd::index<F>, urd::index<Schema>>, urd::dense, urd::layered, Threading>;

private:
    Family<Pools, Ts...> m_relations;
    Family<PoolSchemas, Ts...> m_pool_schemas;
    Schemas m_schemas;
    std::uint8_t m_object_index_width;

public:
    /// The pools of family F, as this layer reads them.
    template<typename F>
        requires HasRows<RelationRepositoryBase, F>
    [[nodiscard]] const Pools<F>& get() const
    { return m_relations.template get<F>(); }

    /// The id of the row of these values in the pool `relation`, in whichever layer holds it, or none.
    template<typename F, std::ranges::input_range Values>
        requires HasRows<RelationRepositoryBase, F>
    [[nodiscard]] urd::index<Row<F>> find(urd::index<F> relation, const Values& values) const
    {
        const auto key = urd::prehashed<Values> { values, urd::hash_range(values) };  // hashed once for every layer
        return get<F>().visit(relation, [&key](const Rows<F>& rows) { return rows.find(key); }, urd::index<Row<F>>());
    }

    /// The number of rows of the pool `relation`, along the layers.
    template<typename F>
        requires HasRows<RelationRepositoryBase, F>
    [[nodiscard]] size_t size(urd::index<F> relation) const
    {
        return get<F>().visit(relation, [](const Rows<F>& rows) { return size_t(rows.size()); }, size_t { 0 });
    }

    /// The Schema `record`, interned here where no layer holds it (record.index set to it).
    urd::index<Schema> schema(urd::data<Schema>& record) { return intern(m_schemas, record).first; }
    /// The column types of a Schema.
    [[nodiscard]] std::span<const TypeId> types(urd::index<Schema> schema) const
    {
        const auto& record = m_schemas[schema];
        return { record.types.data(), record.types.size() };
    }
    /// The Schema of the pool `relation`, or none where no layer declared it.
    template<typename F>
        requires HasRows<RelationRepositoryBase, F>
    [[nodiscard]] urd::index<Schema> schema(urd::index<F> relation) const
    {
        return m_pool_schemas.template get<F>().visit(relation, [](urd::index<Schema> held) { return held; }, urd::index<Schema>());
    }
    /// The column types of the pool `relation` (its Schema's); std::out_of_range where no layer declared it.
    template<typename F>
        requires HasRows<RelationRepositoryBase, F>
    [[nodiscard]] auto types(urd::index<F> relation) const
    {
        const auto held = schema(relation);
        if (held.is_none())
            throw std::out_of_range("RelationRepository: no layer declared the pool.");
        return types(held);
    }

    /// Declares the pool `relation` with the Schema `schema`, once (again with the same Schema changes nothing; another
    /// one, here or in a parent, is std::invalid_argument). Makes no pool: its first row does.
    template<typename F>
        requires HasRows<RelationRepositoryBase, F>
    void declare(urd::index<F> relation, urd::index<Schema> schema)
    {
        if (relation.is_none())
            throw std::invalid_argument("RelationRepository: the maximum relation index is reserved.");
        if (const auto declared = this->schema(relation); !declared.is_none())
        {
            if (declared != schema)
                throw std::invalid_argument("RelationRepository: the pool was declared with other column types.");
            return;
        }
        m_pool_schemas.template get<F>().try_emplace(relation, schema);
    }

    /// The id of the row of these values in the declared pool `relation`, stored in this layer's own pool where no layer
    /// holds it, and whether it was stored now. The values are checked against the pool's Schema: one per column
    /// (std::invalid_argument otherwise), each fitting its column (std::out_of_range otherwise). A layer makes its own
    /// pool, with the Schema's widths, for the first row no layer holds; a pool never declared is std::out_of_range.
    template<typename F, std::ranges::input_range Values>
        requires HasRows<RelationRepositoryBase, F>
    std::pair<urd::index<Row<F>>, bool> insert(urd::index<F> relation, const Values& values)
    {
        auto& relations = m_relations.template get<F>();
        auto* rows = relations.own_value(relation);
        if (rows == nullptr)
        {
            if (const auto found = find(relation, values); !found.is_none())
                return { found, false };
            const auto declared = schema(relation);
            if (declared.is_none())
                throw std::out_of_range("RelationRepository: no layer declared the pool.");
            relations.try_emplace(relation, widths(types(declared)));
            rows = relations.own_value(relation);
        }
        return rows->insert(urd::prehashed<Values> { values, urd::hash_range(values) });  // hashed once for every layer
    }

    /// The bits of an object column: the layer's object index width (urd::bit), else 32 (urd::word).
    [[nodiscard]] std::uint8_t object_bits() const noexcept
    {
        constexpr bool packed = std::same_as<typename RelationRepositoryTraits<ObjectTag>::width, urd::bit>;
        return packed ? m_object_index_width : default_object_index_width.bits;
    }
    /// The widths of this layer's rows of these column types: each whole, an object column object_bits(). The one width
    /// computation of pools and of the layer's builders.
    template<std::ranges::input_range Types>
    [[nodiscard]] urd::column_widths widths(Types&& types) const
    {
        auto result = urd::column_widths();
        for (const TypeId type : types)
            result.push_back(column_bits(type, object_bits()));
        return result;
    }

    /// Parent layers must remain frozen and outlive this repository.
    explicit RelationRepositoryBase(const RelationRepositoryBase* parent = nullptr, urd::bit_width object_index_width = default_object_index_width) :
        m_relations(parent == nullptr ? nullptr : &parent->m_relations),
        m_pool_schemas(parent == nullptr ? nullptr : &parent->m_pool_schemas),
        m_schemas(urd::layer { parent == nullptr ? 0 : parent->m_schemas.layer() + 1 }),
        m_object_index_width(object_index_width.bits)
    {
        if (parent != nullptr)
            m_schemas.layer_over(&parent->m_schemas);
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
    { return urd::memory_usage(get<F>()); }

    /// Forgets this layer's own rows; the parent's stay, and so do the Schemas and the pools' declarations (as the
    /// Repository keeps the pool keys).
    void clear() { m_relations.clear(); }
};
}  // namespace detail

template<typename ObjectTag, typename... Ts>
class RelationRepository : public detail::RelationRepositoryBase<ObjectTag, false, Ts...>
{
public:
    explicit RelationRepository(const RelationRepository* parent = nullptr, urd::bit_width object_index_width = default_object_index_width) :
        detail::RelationRepositoryBase<ObjectTag, false, Ts...>(parent, object_index_width)
    {
    }
};

template<typename ObjectTag, typename... Ts>
class ConcurrentRelationRepository : public detail::RelationRepositoryBase<ObjectTag, true, Ts...>
{
public:
    explicit ConcurrentRelationRepository(const ConcurrentRelationRepository* parent = nullptr,
                                          urd::bit_width object_index_width = default_object_index_width) :
        detail::RelationRepositoryBase<ObjectTag, true, Ts...>(parent, object_index_width)
    {
    }
};
}  // namespace ygg::formalism

#endif
