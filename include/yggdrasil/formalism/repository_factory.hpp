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

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <yggdrasil/core/bit.hpp>
#include <yggdrasil/formalism/repository.hpp>

namespace ygg::formalism
{

template<typename SymbolRepo, typename RelationRepo>
class RepositoryFactory
{
private:
    static RelationRepositoryConfig make_relation_repository_config(size_t num_objects,
                                                                     const Repository<SymbolRepo, RelationRepo>* parent)
    {
        if (num_objects > std::numeric_limits<ygg::uint_t>::max())
            throw std::invalid_argument("RepositoryFactory: number of objects exceeds the object index domain.");

        auto config = RelationRepositoryConfig(static_cast<std::uint8_t>(ygg::bit::bits_needed(num_objects)));
        if (parent)
            config.object_index_width = std::max(config.object_index_width, parent->get_object_index_width());
        return config;
    }

public:
    RepositoryFactory() : m_next_index(0) {}

    Repository<SymbolRepo, RelationRepo> create(const Repository<SymbolRepo, RelationRepo>* parent = nullptr)
    {
        return Repository<SymbolRepo, RelationRepo>(m_next_index++, parent);
    }

    Repository<SymbolRepo, RelationRepo> create(RelationRepositoryConfig config, const Repository<SymbolRepo, RelationRepo>* parent = nullptr)
    {
        return Repository<SymbolRepo, RelationRepo>(m_next_index++, parent, config);
    }

    Repository<SymbolRepo, RelationRepo> create(size_t num_objects, const Repository<SymbolRepo, RelationRepo>* parent = nullptr)
    {
        return create(make_relation_repository_config(num_objects, parent), parent);
    }

    std::shared_ptr<Repository<SymbolRepo, RelationRepo>> create_shared(const Repository<SymbolRepo, RelationRepo>* parent = nullptr)
    {
        return std::make_shared<Repository<SymbolRepo, RelationRepo>>(m_next_index++, parent);
    }

    std::shared_ptr<Repository<SymbolRepo, RelationRepo>> create_shared(RelationRepositoryConfig config,
                                                                        const Repository<SymbolRepo, RelationRepo>* parent = nullptr)
    {
        return std::make_shared<Repository<SymbolRepo, RelationRepo>>(m_next_index++, parent, config);
    }

    std::shared_ptr<Repository<SymbolRepo, RelationRepo>> create_shared(size_t num_objects,
                                                                        const Repository<SymbolRepo, RelationRepo>* parent = nullptr)
    {
        return create_shared(make_relation_repository_config(num_objects, parent), parent);
    }

private:
    ygg::uint_t m_next_index;
};

}  // namespace ygg::formalism

#endif
