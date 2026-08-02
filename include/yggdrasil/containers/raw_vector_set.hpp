#ifndef YGG_CONTAINERS_RAW_VECTOR_SET_HPP_
#define YGG_CONTAINERS_RAW_VECTOR_SET_HPP_

#include "yggdrasil/containers/detail/basic_raw_set.hpp"
#include "yggdrasil/containers/raw_vector_pool.hpp"
#include "yggdrasil/core/concepts.hpp"

#include <memory>

namespace ygg
{

/// ThreadSafe permits concurrent lookup, insertion, size queries, and reads
/// after publication was observed through size() or external synchronization.
/// Clear, memory inspection, move, and destruction require quiescence.
template<std::unsigned_integral Size, TriviallyCopyable T, size_t FirstSegmentBytes = 1024, bool ThreadSafe = false>
class RawVectorSet : public detail::BasicRawSet<RawVectorPool<Size, T, FirstSegmentBytes, ThreadSafe>>
{
private:
    using pool_type = RawVectorPool<Size, T, FirstSegmentBytes, ThreadSafe>;
    using Base = detail::BasicRawSet<pool_type>;

public:
    RawVectorSet() : Base(std::make_unique<pool_type>()) {}
};

}  // namespace ygg

#endif
