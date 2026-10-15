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

#ifndef YGG_FORMALISM_PACKED_RELATION_REPOSITORY_HPP_
#define YGG_FORMALISM_PACKED_RELATION_REPOSITORY_HPP_

#include "yggdrasil/containers/packed_object_row.hpp"
#include "yggdrasil/core/types.hpp"
#include "yggdrasil/formalism/basic_relation_repository.hpp"
#include "yggdrasil/formalism/binding_data.hpp"
#include "yggdrasil/formalism/binding_index.hpp"
#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/containers/detail/slot_directory.hpp"

#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ygg::formalism
{

/// Relation repository whose rows are the bit-packed objects of a binding: lookups encode, views decode,
/// and nothing is stored per binding. Only the root layer owns rows, so every layer resolves a binding
/// to the same row and canonical context. A relation's layout is set once before its first binding;
/// otherwise each argument uses the full object index width.
template<typename ObjectTag, typename T, bool ThreadSafe = false>
class PackedRelationRepository
{
private:
    using ObjectIndex = Index<Object<ObjectTag>>;
    using Layout = PackedRowLayout<ObjectIndex>;

    struct Slot
    {
        std::unique_ptr<Layout> layout;

        explicit Slot(Layout layout) : layout(std::make_unique<Layout>(std::move(layout))) {}
    };

    const PackedRelationRepository* m_parent;
    std::uint8_t m_object_index_width;
    mutable ::ygg::detail::SlotDirectory<Slot, ThreadSafe> m_layouts;

    bool is_root() const noexcept { return m_parent == nullptr; }

    const Layout& get_or_create_layout(Index<T> g, size_t arity) const
    {
        if (const auto* layout = find_layout(g))
            return *layout;
        if (g == Index<T>::max())
            throw std::invalid_argument("PackedRelationRepository: the maximum relation index is reserved.");
        return *m_layouts.get_or_create(ygg::uint_t(g), make_layout(g, [&] { return Layout(arity, m_object_index_width); })).layout;
    }

    template<typename Make>
    static Layout make_layout(Index<T> g, Make&& make)
    {
        try
        {
            return make();
        }
        catch (const std::length_error& error)
        {
            throw std::length_error("PackedRelationRepository: relation " + std::to_string(ygg::uint_t(g)) + ": " + error.what());
        }
    }

    std::optional<Index<Row>> encode(const Data<RelationBinding<T, ObjectTag>>& builder) const
    {
        if (!is_root())
            return std::nullopt;
        const auto& layout = get_or_create_layout(builder.relation, builder.objects.size());
        assert(layout.arity() == builder.objects.size());
        if (const auto row = layout.encode(builder.objects))
            return Index<Row>(*row);
        return std::nullopt;
    }

public:
    static constexpr bool thread_safe = ThreadSafe;
    /// Bindings are encoded rather than stored: insert never creates one and the layer sizes stay 0.
    static constexpr bool stores_bindings = false;

    using container_type = Layout;

    /// Packs the arguments of relation g by their positions in the sorted argument domains.
    void set_layout(Index<T> g, std::span<const std::vector<ObjectIndex>> domains)
    {
        set_layout(g, make_layout(g, [&] { return Layout(domains); }));
    }

    void set_layout(Index<T> g, Layout layout)
    {
        if (!is_root())
            throw std::logic_error("PackedRelationRepository: layouts belong to the root layer.");
        if (find_layout(g))
            throw std::logic_error("PackedRelationRepository: the layout of a relation is set once, before its first binding.");
        m_layouts.get_or_create(ygg::uint_t(g), std::move(layout));
    }

    const Layout* find_layout(Index<T> g) const noexcept
    {
        if (g == Index<T>::max())
            return nullptr;
        const auto* slot = m_layouts.find(ygg::uint_t(g));
        return slot ? slot->layout.get() : nullptr;
    }

    /**
     * Local methods access only the current repository layer.
     */

    std::optional<Index<Row>> find_local_with_hash(const Data<RelationBinding<T, ObjectTag>>& builder, size_t) const { return encode(builder); }

    std::optional<Index<Row>> find_local_unsafe_with_hash(const Data<RelationBinding<T, ObjectTag>>& builder, size_t) const { return encode(builder); }

    std::optional<Index<Row>> find_local(const Data<RelationBinding<T, ObjectTag>>& builder) const { return encode(builder); }

    std::pair<Index<Row>, bool> insert_local_with_hash(const Data<RelationBinding<T, ObjectTag>>& builder, size_t h) { return create_local_with_hash(builder, h); }

    std::pair<Index<Row>, bool> insert_local(const Data<RelationBinding<T, ObjectTag>>& builder) { return create_local_with_hash(builder, 0); }

    /// Every encodable binding exists in the root layer, so a hierarchy-wide miss means that an object lies outside its domain.
    std::pair<Index<Row>, bool> create_local_with_hash(const Data<RelationBinding<T, ObjectTag>>& builder, size_t)
    {
        if (const auto row = encode(builder))
            return { *row, false };
        throw std::out_of_range("PackedRelationRepository: a binding object lies outside its argument domain.");
    }

    PackedObjectRow<ObjectIndex> at_local(Index<RelationBinding<T, ObjectTag>> index) const
    {
        if (!is_local(index))
            throw std::out_of_range("Relation binding index not found in local repository.");
        return PackedObjectRow<ObjectIndex>(ygg::uint_t(index.row), *find_layout(index.relation));
    }

    PackedObjectRow<ObjectIndex> front_local(Index<T> g) const { return at_local(Index<RelationBinding<T, ObjectTag>> { g, Index<Row>(0) }); }

    /// Nothing is stored per binding, so no layer holds rows.
    size_t local_size(Index<T>) const noexcept { return 0; }
    size_t size(Index<T>) const noexcept { return 0; }
    size_t parent_size(Index<T>) const noexcept { return 0; }

    bool is_local(Index<RelationBinding<T, ObjectTag>> index) const noexcept
    {
        const auto& [g, row] = index;
        const auto* layout = is_root() ? find_layout(g) : nullptr;
        return layout && row != Index<Row>::max() && size_t(ygg::uint_t(row)) < layout->capacity();
    }

    bool exists_parent_mutation(Index<T>) const noexcept { return false; }

    /**
     * Common methods do not depend on lookup scope.
     */

    PackedRelationRepository(const PackedRelationRepository* parent = nullptr) : PackedRelationRepository(parent, RelationRepositoryConfig()) {}

    PackedRelationRepository(const PackedRelationRepository* parent, RelationRepositoryConfig config) :
        m_parent(parent),
        m_object_index_width(config.object_index_width),
        m_layouts()
    {
        if (m_parent && m_object_index_width < m_parent->m_object_index_width)
            throw std::invalid_argument("PackedRelationRepository: child object index width must not be smaller than its parent's.");
    }
    PackedRelationRepository(const PackedRelationRepository& other) = delete;
    PackedRelationRepository& operator=(const PackedRelationRepository& other) = delete;
    PackedRelationRepository(PackedRelationRepository&&) = delete;
    PackedRelationRepository& operator=(PackedRelationRepository&&) = delete;

    /// Layouts configure the relations and survive clearing; no bindings are stored.
    void clear() noexcept {}

    size_t memory_usage() const noexcept
    {
        size_t bytes = m_layouts.memory_usage();
        m_layouts.for_each([&](const Slot& slot) { bytes += sizeof(Layout) + slot.layout->memory_usage(); });
        return bytes;
    }

    std::uint8_t get_object_index_width() const noexcept { return m_object_index_width; }

    static size_t hash(const Data<RelationBinding<T, ObjectTag>>&) noexcept { return 0; }
};

}  // namespace ygg::formalism

#endif
