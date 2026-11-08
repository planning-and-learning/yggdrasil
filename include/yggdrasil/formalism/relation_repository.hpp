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
#include "yggdrasil/formalism/binding_data.hpp"
#include "yggdrasil/formalism/declarations.hpp"

#include <boost/mp11.hpp>
#include <concepts>
#include <cstdint>
#include <limits>
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

/// How a relation repository stores its bindings' objects, per object tag: each index whole (urd::word), or in the
/// object_index_width bits the largest needs (urd::bit).
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
/// The bindings of a repository, per relation type in a urd family: per relation, a pool of its bindings' rows of
/// objects (each row once, its index the binding's row), in a dense map by relation. Over a parent, both levels layer:
/// a relation this layer never touched reads the parent's pool; one it touched has a pool of its own over the parent's,
/// its rows numbered after the parent's.
template<typename ObjectTag, bool ThreadSafe, typename... Ts>
class RelationRepositoryBase
{
    using Threading = std::conditional_t<ThreadSafe, urd::concurrent, urd::single_threaded>;
    using Objects = urd::flat_vector<urd::index<Object<ObjectTag>>>;

public:
    using object_tag = ObjectTag;
    using RelationTypes = boost::mp11::mp_list<Ts...>;
    static constexpr bool thread_safe = ThreadSafe;

    /// One relation's rows.
    using Rows = urd::Store<urd::pool<urd::Store<urd::sequence<urd::index<Object<ObjectTag>>>, urd::frozen>, urd::index<Row>>,
                            urd::monotone_until_clear,
                            typename RelationRepositoryTraits<ObjectTag>::width,
                            urd::layered,
                            Threading>;
    /// The rows of each relation of type T.
    template<typename T>
    using Relations = urd::Store<urd::map<urd::index<T>, Rows>, urd::dense, urd::layered, Threading>;

private:
    urd::utils::Family<Relations, Ts...> m_relations;
    std::uint8_t m_object_index_width;

    template<typename T>
    static urd::prehashed<Objects> key(const urd::data<RelationBinding<T, ObjectTag>>& builder)
    { return { builder.objects, urd::hash_range(builder.objects) }; }

public:
    /// The relations of type T, as this layer reads them.
    template<typename T>
        requires SupportsRelation<RelationRepositoryBase, T>
    [[nodiscard]] const Relations<T>& get() const
    { return m_relations.template get<T>(); }

    /// The binding's row, in whichever layer holds it, or none.
    template<typename T>
        requires SupportsRelation<RelationRepositoryBase, T>
    [[nodiscard]] urd::index<Row> find(const urd::data<RelationBinding<T, ObjectTag>>& builder) const
    { return get<T>().read(builder.relation, [&builder](const Rows& rows) { return rows.find(key(builder)); }, urd::index<Row>()); }

    /// The binding's row, stored in this layer's own pool of its relation where no layer holds it, and whether it was
    /// stored now. Every row of a relation has one object per argument (checked against its first).
    template<typename T>
        requires SupportsRelation<RelationRepositoryBase, T>
    std::pair<urd::index<Row>, bool> insert(const urd::data<RelationBinding<T, ObjectTag>>& builder)
    {
        if (builder.relation.is_none())
            throw std::invalid_argument("RelationRepository: the maximum relation index is reserved.");
        auto& relations = m_relations.template get<T>();
        if constexpr (std::constructible_from<Rows, urd::bit_width>)
            relations.try_emplace(builder.relation, urd::bit_width { m_object_index_width });
        auto& rows = relations.at(builder.relation);
        if (rows.size() != 0 && rows.row_size(urd::index<Row>(0)) != builder.objects.size())
            throw std::invalid_argument("RelationRepository: wrong number of objects for the relation.");
        return rows.insert(key(builder));
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

    /// This layer's own storage of T's bindings.
    template<typename T>
        requires SupportsRelation<RelationRepositoryBase, T>
    [[nodiscard]] size_t memory_usage() const
    { return urd::memory_usage(get<T>()); }

    /// Forgets this layer's own bindings; the parent's stay.
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
