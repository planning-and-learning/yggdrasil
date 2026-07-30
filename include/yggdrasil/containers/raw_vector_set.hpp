#ifndef YGG_CONTAINERS_RAW_VECTOR_SET_HPP_
#define YGG_CONTAINERS_RAW_VECTOR_SET_HPP_

#include "yggdrasil/containers/detail/lazy_insert.hpp"
#include "yggdrasil/containers/raw_vector_pool.hpp"
#include "yggdrasil/core/concepts.hpp"
#include "yggdrasil/core/config.hpp"
#include "yggdrasil/semantics/equal_to.hpp"
#include "yggdrasil/semantics/hash.hpp"

#include <cassert>
#include <gtl/phmap.hpp>
#include <memory>
#include <optional>
#include <span>
#include <utility>

namespace ygg
{

template<std::unsigned_integral Size, TriviallyCopyable T, size_t FirstSegmentBytes = 1024>
class RawVectorSet
{
public:
    RawVectorSet() : m_pool(std::make_shared<RawVectorPool<Size, T, FirstSegmentBytes>>()), m_set(0, IndexableHash(m_pool), IndexableEqualTo(m_pool)) {}

    RawVectorSet(const RawVectorSet&) = delete;
    RawVectorSet& operator=(const RawVectorSet&) = delete;
    RawVectorSet(RawVectorSet&&) = default;
    RawVectorSet& operator=(RawVectorSet&&) = default;

    std::optional<uint_t> find(std::span<const T> value) const
    {
        if (auto it = m_set.find(value); it != m_set.end())
            return *it;
        return std::nullopt;
    }

    bool contains(std::span<const T> value) const { return m_set.contains(value); }

    uint_t insert(std::span<const T> value)
    {
        const auto hash = m_set.hash(value);
        const auto result = detail::lazy_insert_with_hash(m_set, value, hash, [&] { return m_pool->insert(value); });
        return *result.first;
    }

    RawVectorView<Size, T> operator[](uint_t idx) noexcept { return (*m_pool)[idx]; }

    RawVectorView<const Size, const T> operator[](uint_t idx) const noexcept { return std::as_const(*m_pool)[idx]; }

    RawVectorView<Size, T> at(uint_t idx) { return m_pool->at(idx); }

    RawVectorView<const Size, const T> at(uint_t idx) const { return std::as_const(*m_pool).at(idx); }

    RawVectorView<Size, T> front() { return m_pool->front(); }

    RawVectorView<const Size, const T> front() const { return std::as_const(*m_pool).front(); }

    RawVectorView<Size, T> back() { return m_pool->back(); }

    RawVectorView<const Size, const T> back() const { return std::as_const(*m_pool).back(); }

    size_t memory_usage() const noexcept
    {
        size_t bytes = 0;
        bytes += m_pool ? m_pool->memory_usage() : 0;
        bytes += m_set.capacity() * (sizeof(uint_t) + sizeof(gtl::priv::ctrl_t));
        return bytes;
    }

    size_t size() const noexcept { return m_pool->size(); }
    bool empty() const noexcept { return m_pool->empty(); }

    void clear() noexcept
    {
        m_pool->clear();
        m_set.clear();
    }

private:
    struct IndexableHash
    {
        using is_transparent = void;

        std::shared_ptr<RawVectorPool<Size, T, FirstSegmentBytes>> pool;

        IndexableHash() noexcept : pool(nullptr) {}
        explicit IndexableHash(std::shared_ptr<RawVectorPool<Size, T, FirstSegmentBytes>> pool) noexcept : pool(std::move(pool)) {}

        size_t operator()(uint_t idx) const noexcept { return ygg::hash_range((*pool)[idx]); }

        size_t operator()(std::span<const T> value) const noexcept { return ygg::hash_range(value); }
    };

    struct IndexableEqualTo
    {
        using is_transparent = void;

        std::shared_ptr<RawVectorPool<Size, T, FirstSegmentBytes>> pool;

        IndexableEqualTo() noexcept : pool(nullptr) {}
        explicit IndexableEqualTo(std::shared_ptr<RawVectorPool<Size, T, FirstSegmentBytes>> pool) noexcept : pool(std::move(pool)) {}

        bool operator()(uint_t lhs, uint_t rhs) const noexcept { return equal_range((*pool)[lhs], (*pool)[rhs]); }

        bool operator()(std::span<const T> lhs, uint_t rhs) const noexcept { return equal_range(lhs, (*pool)[rhs]); }

        bool operator()(uint_t lhs, std::span<const T> rhs) const noexcept { return equal_range((*pool)[lhs], rhs); }

        bool operator()(std::span<const T> lhs, std::span<const T> rhs) const noexcept { return equal_range(lhs, rhs); }
    };

    std::shared_ptr<RawVectorPool<Size, T, FirstSegmentBytes>> m_pool;
    gtl::flat_hash_set<uint_t, IndexableHash, IndexableEqualTo> m_set;
};

}  // namespace ygg

#endif
