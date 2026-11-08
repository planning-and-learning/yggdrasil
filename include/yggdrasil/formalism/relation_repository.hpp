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
#include "yggdrasil/formalism/row_data.hpp"
#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/values.hpp"

#include <boost/mp11.hpp>
#include <concepts>
#include <cstdint>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <urd/elements/flat.hpp>
#include <urd/elements/hash.hpp>
#include <urd/elements/prehashed.hpp>
#include <urd/memory.hpp>
#include <urd/repr/maps/index_map.hpp>
#include <urd/repr/pools/pools.hpp>
#include <urd/repr/wrappers/layer.hpp>
#include <urd/repr/wrappers/layered_pool.hpp>
#include <urd/store.hpp>
#include <urd/utils/family.hpp>
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
/// The rows of a repository, per relation family F in a urd family: per pool key (an index<F>), a pool of its rows
/// (each row once, its index the row's id, an index<Row<F>>), in a dense map by key. A family of objects (a
/// predicate's rows) has rows of objects; the Schema family keys free relations' pools by their column types. Every
/// pool is built with the widths widths() gives its column types. Over a parent, both levels layer: a pool this layer
/// never touched reads the parent's; one it touched has a pool of its own over the parent's, its rows numbered after
/// the parent's.
template<typename ObjectTag, bool ThreadSafe, typename... Ts>
class RelationRepositoryBase
{
    using Threading = std::conditional_t<ThreadSafe, urd::concurrent, urd::single_threaded>;

public:
    using object_tag = ObjectTag;
    using RelationTypes = boost::mp11::mp_list<Ts...>;
    static constexpr bool thread_safe = ThreadSafe;

    /// One pool's rows: a Value per column, read as row_value_t<F> (see RelationRepositoryTraits for object columns).
    template<typename F>
    using Rows = urd::Store<urd::pool<urd::Store<urd::sequence<Value>, urd::frozen>, urd::index<Row<F>>>,
                            urd::monotone_until_clear,
                            urd::by_column_widths,
                            urd::layered,
                            Threading>;
    /// The pools of family F, by key. Clear keeps each slot's pool (constant_clear: a generation map), which a pool
    /// made again with the same widths reuses, storage and all.
    template<typename F>
    using Relations = urd::Store<urd::map<urd::index<F>, Rows<F>>, urd::dense, urd::layered, urd::constant_clear, Threading>;

private:
    urd::utils::Family<Relations, Ts...> m_relations;
    std::uint8_t m_object_index_width;

    template<typename F>
    static urd::prehashed<urd::flat_vector<row_value_t<F>>> key(const urd::data<Row<F>>& row)
    { return { row.values, urd::hash_columns(row.values) }; }

public:
    /// The pools of family F, as this layer reads them.
    template<typename F>
        requires HasRows<RelationRepositoryBase, F>
    [[nodiscard]] const Relations<F>& get() const
    { return m_relations.template get<F>(); }

    /// The row's id, in whichever layer holds it, or none.
    template<typename F>
        requires HasRows<RelationRepositoryBase, F>
    [[nodiscard]] urd::index<Row<F>> find(const urd::data<Row<F>>& row) const
    { return get<F>().read(row.relation, [&row](const Rows<F>& rows) { return rows.find(key(row)); }, urd::index<Row<F>>()); }

    /// The number of rows of the pool `relation`, along the layers.
    template<typename F>
        requires HasRows<RelationRepositoryBase, F>
    [[nodiscard]] size_t size(urd::index<F> relation) const
    { return get<F>().read(relation, [](const Rows<F>& rows) { return size_t(rows.size()); }, size_t { 0 }); }

    /// The row's id, stored in this layer's own pool of its relation where no layer holds it, and whether it was stored
    /// now. The pool, made where missing, has the widths of `types`, the pool's column types; every row of a pool has one
    /// value per column (checked against the pool's columns).
    template<typename F, std::ranges::input_range Types>
        requires HasRows<RelationRepositoryBase, F>
    std::pair<urd::index<Row<F>>, bool> insert(const urd::data<Row<F>>& row, Types&& types)
    {
        if (row.relation.is_none())
            throw std::invalid_argument("RelationRepository: the maximum relation index is reserved.");
        auto& rows = own_rows(row.relation, [&] { return widths(types); });
        if (rows.column_widths().size() != row.values.size())
            throw std::invalid_argument("RelationRepository: wrong number of values for the relation.");
        return rows.insert(key(row));
    }

    /// This layer's own pool of `relation`, made where it has none with the widths `widths()` gives (over the parent's
    /// pool, if any).
    template<typename F, std::invocable Widths>
        requires HasRows<RelationRepositoryBase, F>
    Rows<F>& own_rows(urd::index<F> relation, Widths widths)
    {
        auto& relations = m_relations.template get<F>();
        if (!relations.read_own(relation, [](const Rows<F>&) { return true; }, false))
            relations.try_emplace(relation, widths());
        return relations.at(relation);
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
    { return urd::memory_usage(get<F>()); }

    /// Forgets this layer's own rows; the parent's stay.
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
