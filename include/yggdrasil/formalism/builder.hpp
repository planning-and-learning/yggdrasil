/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_FORMALISM_BUILDER_HPP_
#define YGG_FORMALISM_BUILDER_HPP_

#include "yggdrasil/containers/unique_object_pool.hpp"
#include "yggdrasil/core/concepts.hpp"
#include "yggdrasil/core/type_list.hpp"
#include "yggdrasil/core/types.hpp"

#include <tuple>

namespace ygg::formalism
{

template<typename... Ts>
class BuilderStorage
{
    std::tuple<UniqueObjectPool<Data<Ts>>...> m_data;

public:
    using Types = TypeList<Ts...>;

    BuilderStorage() = default;
    BuilderStorage(const BuilderStorage&) = delete;
    BuilderStorage& operator=(const BuilderStorage&) = delete;
    BuilderStorage(BuilderStorage&&) = delete;
    BuilderStorage& operator=(BuilderStorage&&) = delete;

    /// Acquire pooled data without resetting its contents.
    template<typename T>
        requires(Types::template contains<T>)
    [[nodiscard]] auto get_builder()
    {
        return std::get<UniqueObjectPool<Data<T>>>(m_data).get_or_allocate();
    }

    /// Clear pooled data once, retaining its reusable buffers.
    template<typename T>
        requires(Types::template contains<T> && Clearable<Data<T>>)
    [[nodiscard]] auto checkout()
    {
        auto data = get_builder<T>();
        data->clear();
        return data;
    }
};

template<typename T, typename... Ts>
    requires requires(BuilderStorage<Ts...>& builder) { builder.template checkout<T>(); }
[[nodiscard]] auto checkout(BuilderStorage<Ts...>& builder)
{
    return builder.template checkout<T>();
}

}  // namespace ygg::formalism

#endif
