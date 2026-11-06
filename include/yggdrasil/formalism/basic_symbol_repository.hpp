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

#ifndef YGG_FORMALISM_BASIC_SYMBOL_REPOSITORY_HPP_
#define YGG_FORMALISM_BASIC_SYMBOL_REPOSITORY_HPP_

#include "yggdrasil/core/config.hpp"
#include "yggdrasil/formalism/declarations.hpp"
#include <urd/elements/flat.hpp>

#include <cassert>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <urd/elements/equal_to.hpp>
#include <urd/elements/hash.hpp>
#include <urd/elements/prehashed.hpp>
#include <urd/memory.hpp>
#include <urd/repr/records/arena.hpp>
#include <urd/repr/records/records.hpp>
#include <urd/store.hpp>
#include <utility>

namespace ygg::formalism
{

/// Construction, value lookup (find_local*), and the write API (insert_local*, create_local_with_hash) are defined in
/// detail/basic_symbol_repository_impl.hpp, which needs cista/serialization.h for serialized symbols; sources that
/// instantiate the repository include it.
template<typename T, bool ThreadSafe = false>
class BasicSymbolRepository
{
private:
    /// The layer's records: a urd pool of them, each once, read by index; the request decides how they are stored
    /// (in place or serialized into an arena), with references that hold while others are inserted (views borrow them).
    struct Slot
    {
        using Records = urd::Store<urd::pool<urd::data<T>>, urd::flat, urd::stable_refs, std::conditional_t<ThreadSafe, urd::concurrent, urd::single_threaded>>;

        Records records { urd::layer { 0 } };
        size_t parent_size = 0;

        static size_t hash(const urd::data<T>& builder) noexcept { return urd::Hash<urd::data<T>> {}(builder); }
        [[nodiscard]] const urd::data<T>& operator[](size_t local) const noexcept { return records[urd::index<urd::data<T>>(static_cast<uint_t>(local))]; }

        void clear() noexcept { records.clear(); }
        [[nodiscard]] size_t memory_usage() const noexcept { return urd::memory_usage(records); }
    };

    const BasicSymbolRepository* m_parent;
    Slot m_slot;

    void clear_slot() noexcept
    {
        m_slot.clear();
        m_slot.parent_size = m_parent ? m_parent->size() : size_t { 0 };
    }

public:
    static constexpr bool thread_safe = ThreadSafe;

    /**
     * Local methods access only the current repository layer.
     * Handle-producing methods return raw handles because the caller already
     * knows the context.
     */

    [[nodiscard]] std::optional<urd::index<T>> find_local_with_hash(const urd::data<T>& builder, size_t h) const noexcept;

    [[nodiscard]] std::optional<urd::index<T>> find_local(const urd::data<T>& builder) const noexcept
    { return find_local_with_hash(builder, BasicSymbolRepository::hash(builder)); }

    std::pair<urd::index<T>, bool> insert_local_with_hash(urd::data<T>& builder, size_t h);

    std::pair<urd::index<T>, bool> insert_local(urd::data<T>& builder);

    /// Completes a hierarchy-wide miss by rechecking this layer before publishing
    /// storage.
    std::pair<urd::index<T>, bool> create_local_with_hash(urd::data<T>& builder, size_t h);

    /// Unchecked, like std::vector::operator[]: index must be local (asserted in Debug).
    [[nodiscard]] const urd::data<T>& local(urd::index<T> index) const noexcept
    {
        assert(is_local(index));
        return m_slot[index.value - m_slot.parent_size];
    }

    [[nodiscard]] const urd::data<T>& at_local(urd::index<T> index) const
    {
        if (!is_local(index))
            throw std::out_of_range("Symbol index not found in local repository.");
        return local(index);
    }

    [[nodiscard]] const urd::data<T>& front_local() const
    {
        if (m_slot.records.empty())
            throw std::out_of_range("Symbol index not found in local repository.");
        return m_slot[0];
    }

    [[nodiscard]] size_t local_size() const noexcept { return m_slot.records.size(); }

    [[nodiscard]] size_t size() const noexcept { return m_slot.parent_size + m_slot.records.size(); }

    [[nodiscard]] size_t parent_size() const noexcept { return m_slot.parent_size; }

    [[nodiscard]] bool is_local(urd::index<T> index) const noexcept
    { return index != urd::index<T>() && ygg::uint_t(index.value) >= m_slot.parent_size && ygg::uint_t(index.value) < size(); }

    [[nodiscard]] bool exists_parent_mutation() const noexcept
    {
        if (!m_parent)
            return false;

        return m_parent->size() > m_slot.parent_size;
    }

    /**
     * Common methods do not depend on lookup scope.
     */

    BasicSymbolRepository() : BasicSymbolRepository(nullptr) {}
    explicit BasicSymbolRepository(const BasicSymbolRepository* parent);
    ~BasicSymbolRepository();

    BasicSymbolRepository(const BasicSymbolRepository&) = delete;
    BasicSymbolRepository& operator=(const BasicSymbolRepository&) = delete;
    BasicSymbolRepository(BasicSymbolRepository&&) noexcept
        requires(!ThreadSafe)
    = default;
    BasicSymbolRepository& operator=(BasicSymbolRepository&&) noexcept
        requires(!ThreadSafe)
    = default;
    BasicSymbolRepository(BasicSymbolRepository&&)
        requires ThreadSafe
    = delete;
    BasicSymbolRepository& operator=(BasicSymbolRepository&&)
        requires ThreadSafe
    = delete;

    void clear() noexcept;

    [[nodiscard]] size_t memory_usage() const noexcept;

    static size_t hash(const urd::data<T>& builder) noexcept;
};

template<typename T, bool ThreadSafe>
size_t BasicSymbolRepository<T, ThreadSafe>::hash(const urd::data<T>& builder) noexcept
{ return Slot::hash(builder); }

}  // namespace ygg::formalism

#endif
