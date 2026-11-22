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

#include "yggdrasil/formalism/repository_instantiations.hpp"

#include "yggdrasil/formalism/free_relations.hpp"
#include "yggdrasil/formalism/relation_repository.hpp"
#include "yggdrasil/formalism/repository_impl.hpp"

#include <ranges>
#include <array>
#include <gtest/gtest.h>

// The declarations of a repository's explicit instantiations, as the header that names it writes them, and their
// definitions, as the one source of the library that holds it writes them.
YGG_FORMALISM_REPOSITORY(extern, ygg::FreeRelationRepository, 2, 1)
YGG_FORMALISM_REPOSITORY(, ygg::FreeRelationRepository, 2, 1)

namespace
{
/// The Schema of these column types, inserted into `repository` as any record.
template<std::ranges::input_range Types>
urd::index<ygg::Schema> insert_schema(ygg::FreeRelationRepository& repository, const Types& types)
{
    auto schema = urd::data<ygg::Schema>();
    schema.column_types.set(std::ranges::begin(types), std::ranges::end(types));
    return repository.insert(schema).first.index();
}


TEST(RepositoryInstantiations, InstantiateEveryMemberThatTouchesTheStores)
{
    auto repository = ygg::FreeRelationRepository();
    const auto relation = insert_schema(repository, std::array { ygg::type_id<bool> });
    auto row = urd::data<ygg::Row<ygg::Schema>>(relation, urd::flat_vector<ygg::Value> { ygg::encode(true, ygg::type_id<bool>) });
    const auto [stored, inserted] = repository.insert(row);
    EXPECT_TRUE(inserted);
    EXPECT_EQ(repository.find(row), stored);
    EXPECT_TRUE(stored.get<bool>(0));
}

}  // namespace
