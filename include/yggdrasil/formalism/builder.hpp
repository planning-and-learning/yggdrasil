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
#include "yggdrasil/core/types.hpp"

namespace ygg::formalism
{

template<typename T>
class BasicBuilder
{
private:
    ygg::UniqueObjectPool<ygg::Data<T>> m_data;

public:
    [[nodiscard]] auto get_builder() { return m_data.get_or_allocate(); }
};

template<typename... Ts>
class BuilderStorage : private BasicBuilder<Ts>...
{
public:
    template<typename T>
    [[nodiscard]] auto get_builder()
    {
        return static_cast<BasicBuilder<T>&>(*this).get_builder();
    }
};

}  // namespace ygg::formalism

#endif
