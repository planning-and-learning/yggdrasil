#ifndef YGG_COMMON_RAW_VECTOR_SET_HPP_
#define YGG_COMMON_RAW_VECTOR_SET_HPP_

#include "ygg/core/concepts.hpp"
#include "ygg/core/config.hpp"
#include "ygg/semantics/equal_to.hpp"
#include "ygg/semantics/hash.hpp"
#include "ygg/containers/raw_vector_pool.hpp"

#include <cassert>
#include <memory>
#include <optional>
#include <span>
#include <utility>

#include <gtl/phmap.hpp>

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
        if (auto it = m_set.find(value); it != m_set.end())
            return *it;

        const auto idx = m_pool->insert(value);
        m_set.emplace(idx);
        return idx;
    }

    RawVectorView<Size, T> operator[](uint_t idx) noexcept { return (*m_pool)[idx]; }

    RawVectorView<const Size, const T> operator[](uint_t idx) const noexcept { return std::as_const(*m_pool)[idx]; }

    RawVectorView<Size, T> front() noexcept
    {
        assert(!empty());
        return (*m_pool)[0];
    }

    RawVectorView<const Size, const T> front() const noexcept
    {
        assert(!empty());
        return std::as_const(*m_pool)[0];
    }

    RawVectorView<Size, T> back() noexcept
    {
        assert(!empty());
        return (*m_pool)[to_uint_t(size() - 1)];
    }

    RawVectorView<const Size, const T> back() const noexcept
    {
        assert(!empty());
        return std::as_const(*m_pool)[to_uint_t(size() - 1)];
    }

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

        size_t operator()(uint_t idx) const noexcept { return hash_range((*pool)[idx]); }

        size_t operator()(std::span<const T> value) const noexcept { return hash_range(value); }
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
