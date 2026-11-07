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

#ifndef YGG_FORMALISM_REPOSITORY_FACTORY_HPP_
#define YGG_FORMALISM_REPOSITORY_FACTORY_HPP_

#include <urd/tags.hpp>
#include "yggdrasil/core/index_sequence.hpp"
#include "yggdrasil/formalism/repository.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>

namespace ygg::formalism
{

template<typename SymbolRepo, typename RelationRepo>
class RepositoryFactory
{
private:
    static urd::bit_width object_index_width(size_t num_objects, const Repository<SymbolRepo, RelationRepo>* parent)
    {
        if (num_objects > std::numeric_limits<ygg::uint_t>::max())
            throw std::invalid_argument("RepositoryFactory: number of objects exceeds the object index domain.");

        auto width = urd::bit_width::for_bound(num_objects);
        if (parent)
            width.bits = std::max(width.bits, parent->get_object_index_width());
        return width;
    }

public:
    RepositoryFactory() = default;

    Repository<SymbolRepo, RelationRepo> create(const Repository<SymbolRepo, RelationRepo>* parent = nullptr)
    { return Repository<SymbolRepo, RelationRepo>(m_indices.next(), parent); }

    Repository<SymbolRepo, RelationRepo> create(urd::bit_width object_index_width, const Repository<SymbolRepo, RelationRepo>* parent = nullptr)
    { return Repository<SymbolRepo, RelationRepo>(m_indices.next(), parent, object_index_width); }

    Repository<SymbolRepo, RelationRepo> create(size_t num_objects, const Repository<SymbolRepo, RelationRepo>* parent = nullptr)
    { return create(object_index_width(num_objects, parent), parent); }

    std::shared_ptr<Repository<SymbolRepo, RelationRepo>> create_shared(const Repository<SymbolRepo, RelationRepo>* parent = nullptr)
    { return std::make_shared<Repository<SymbolRepo, RelationRepo>>(m_indices.next(), parent); }

    std::shared_ptr<Repository<SymbolRepo, RelationRepo>> create_shared(urd::bit_width object_index_width,
                                                                        const Repository<SymbolRepo, RelationRepo>* parent = nullptr)
    { return std::make_shared<Repository<SymbolRepo, RelationRepo>>(m_indices.next(), parent, object_index_width); }

    std::shared_ptr<Repository<SymbolRepo, RelationRepo>> create_shared(size_t num_objects, const Repository<SymbolRepo, RelationRepo>* parent = nullptr)
    { return create_shared(object_index_width(num_objects, parent), parent); }

private:
    IndexSequence m_indices;
};

}  // namespace ygg::formalism

#endif
