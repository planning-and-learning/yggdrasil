/*
 * Copyright (C) 2025-2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGGDRASIL_FORMALISM_OBJECT_INDEX_HPP_
#define YGGDRASIL_FORMALISM_OBJECT_INDEX_HPP_

#include <yggdrasil/core/types.hpp>
#include <yggdrasil/formalism/declarations.hpp>
#include <yggdrasil/ids/index_mixins.hpp>

namespace ygg
{
template<typename Tag>
struct Index<ygg::formalism::Object<Tag>> : ygg::IndexMixin<Index<ygg::formalism::Object<Tag>>>
{
    using Base = ygg::IndexMixin<Index<ygg::formalism::Object<Tag>>>;
    using Base::Base;
};
}

#endif
