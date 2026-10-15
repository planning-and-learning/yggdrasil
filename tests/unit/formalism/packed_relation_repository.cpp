/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "yggdrasil/formalism/packed_relation_repository.hpp"

#include "yggdrasil/core/types.hpp"
#include "yggdrasil/extensions.hpp"
#include "yggdrasil/formalism/binding_data.hpp"
#include "yggdrasil/formalism/binding_view.hpp"
#include "yggdrasil/formalism/repository.hpp"

#include "gtest/gtest.h"
#include <cstdint>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <vector>

namespace ygg::tests
{
struct PackedElement;
struct PackedRelation;
struct HashedRelation;
struct PackedObjectTag;
}

namespace ygg
{
template<>
struct Data<tests::PackedElement>
{
    Index<tests::PackedElement> index;
    std::uint64_t value = 0;

    auto identifying_members() const noexcept { return std::tie(value); }
};
}

namespace ygg::tests
{
namespace
{
using Object = formalism::Object<PackedObjectTag>;

template<typename ObjectTag, typename T, bool ThreadSafe>
struct Selector
{
    using type = std::conditional_t<std::same_as<T, PackedRelation>,
                                    formalism::PackedRelationRepository<ObjectTag, T, ThreadSafe>,
                                    formalism::BasicRelationRepository<ObjectTag, T, ThreadSafe>>;
};

using Repository = formalism::Repository<formalism::ConcurrentSymbolRepository<PackedElement>,
                                         formalism::SelectedConcurrentRelationRepository<Selector, PackedObjectTag, PackedRelation, HashedRelation>>;

static_assert(!Repository::stores_bindings<PackedRelation>);
static_assert(Repository::stores_bindings<HashedRelation>);

template<typename R>
Data<formalism::RelationBinding<R, PackedObjectTag>> make_binding(Index<R> relation, std::vector<uint_t> objects)
{
    auto list = IndexList<Object> {};
    for (const auto object : objects)
        list.push_back(Index<Object>(object));
    const auto arity = list.size();
    return Data<formalism::RelationBinding<R, PackedObjectTag>>(relation, arity, std::move(list));
}

std::vector<uint_t> objects_of(const auto& view)
{
    auto result = std::vector<uint_t> {};
    for (const auto object : view.get_objects())
        result.push_back(uint_t(object));
    return result;
}

TEST(YggdrasilTests, PackedRelationRepositoryPacksFullWidthObjectIndices)
{
    auto repository = Repository(0, nullptr, formalism::RelationRepositoryConfig(4));
    const auto relation = Index<PackedRelation>(0);

    const auto [view, created] = repository.insert(make_binding(relation, { 3, 5 }));
    EXPECT_FALSE(created);
    EXPECT_EQ(uint_t(view.get_index().row), 3U | (5U << 4));
    EXPECT_EQ(objects_of(view), (std::vector<uint_t> { 3, 5 }));

    const auto found = repository.find(make_binding(relation, { 3, 5 }));
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->get_index().row, view.get_index().row);
    EXPECT_EQ(objects_of(make_view(view.get_index(), repository)), (std::vector<uint_t> { 3, 5 }));
}

TEST(YggdrasilTests, PackedRelationRepositoryPacksDomainPositions)
{
    auto repository = Repository(0, nullptr, formalism::RelationRepositoryConfig(8));
    const auto relation = Index<PackedRelation>(2);
    const auto domains =
        std::vector<std::vector<Index<Object>>> { { Index<Object>(3), Index<Object>(7), Index<Object>(9) }, { Index<Object>(2), Index<Object>(5) } };
    repository.get_relation_repository<PackedRelation>().set_layout(relation, domains);

    const auto [view, created] = repository.insert(make_binding(relation, { 9, 5 }));
    EXPECT_EQ(uint_t(view.get_index().row), 2U + 1U * 3U);
    EXPECT_EQ(objects_of(view), (std::vector<uint_t> { 9, 5 }));

    EXPECT_FALSE(repository.find(make_binding(relation, { 4, 5 })).has_value());
    EXPECT_THROW(repository.insert(make_binding(relation, { 4, 5 })), std::out_of_range);
    EXPECT_THROW(repository.get_relation_repository<PackedRelation>().set_layout(relation, domains), std::logic_error);
}

TEST(YggdrasilTests, PackedRelationRepositoryRanksBeyondTheBitWidthsOfTheFields)
{
    auto repository = Repository(0, nullptr, formalism::RelationRepositoryConfig(8));
    auto domain = std::vector<Index<Object>> {};
    for (uint_t object = 0; object < 17; ++object)
        domain.push_back(Index<Object>(object * 3));

    // Seven fields of 5 bits would exceed 32 bits, while the rank 17^7 fits.
    const auto relation = Index<PackedRelation>(3);
    repository.get_relation_repository<PackedRelation>().set_layout(relation, std::vector<std::vector<Index<Object>>>(7, domain));
    const auto objects = std::vector<uint_t> { 48, 0, 3, 24, 45, 6, 48 };
    const auto [view, created] = repository.insert(make_binding(relation, objects));
    EXPECT_EQ(objects_of(view), objects);
    EXPECT_EQ(repository.find(make_binding(relation, objects))->get_index().row, view.get_index().row);
    EXPECT_FALSE(repository.find(make_binding(relation, { 48, 0, 3, 24, 45, 6, 1 })).has_value());

    // The rank of eight such fields, 17^8, exceeds 32 bits.
    EXPECT_THROW(repository.get_relation_repository<PackedRelation>().set_layout(Index<PackedRelation>(4), std::vector<std::vector<Index<Object>>>(8, domain)),
                 std::length_error);
}

TEST(YggdrasilTests, PackedRelationRepositoryRejectsObjectsOfAnEmptyDomain)
{
    auto repository = Repository(0, nullptr, formalism::RelationRepositoryConfig(8));
    const auto relation = Index<PackedRelation>(5);
    repository.get_relation_repository<PackedRelation>().set_layout(relation, std::vector<std::vector<Index<Object>>> { {}, { Index<Object>(1) } });
    EXPECT_FALSE(repository.find(make_binding(relation, { 0, 1 })).has_value());
}

TEST(YggdrasilTests, PackedRelationRepositoryRejectsRowsWiderThanTheRowIndex)
{
    auto repository = Repository(0, nullptr, formalism::RelationRepositoryConfig(12));
    EXPECT_THROW(repository.find(make_binding(Index<PackedRelation>(0), { 1, 2, 3 })), std::length_error);
}

TEST(YggdrasilTests, PackedRelationRepositoryResolvesChildBindingsToTheRoot)
{
    auto root = Repository(0, nullptr, formalism::RelationRepositoryConfig(4));
    auto child = Repository(1, &root, formalism::RelationRepositoryConfig(4));
    const auto relation = Index<PackedRelation>(0);

    const auto [view, created] = child.insert(make_binding(relation, { 1, 2 }));
    EXPECT_FALSE(created);
    EXPECT_EQ(view.get_context().get_index(), root.get_index());
    EXPECT_EQ(&child.get_canonical_context(view.get_index()), &root);
    EXPECT_EQ(make_view(view.get_index(), child), make_view(view.get_index(), root));

    // Hashed relations keep layering: the child owns its bindings.
    const auto [hashed, hashed_created] = child.insert(make_binding(Index<HashedRelation>(0), { 1, 2 }));
    EXPECT_TRUE(hashed_created);
    EXPECT_EQ(hashed.get_context().get_index(), child.get_index());
}

TEST(YggdrasilTests, PackedObjectRowsCompareByTheirObjects)
{
    auto repository = Repository(0, nullptr, formalism::RelationRepositoryConfig(4));
    const auto lhs = repository.insert(make_binding(Index<PackedRelation>(0), { 1, 2 })).first;
    const auto rhs = repository.insert(make_binding(Index<PackedRelation>(1), { 1, 2 })).first;
    const auto other = repository.insert(make_binding(Index<PackedRelation>(1), { 2, 1 })).first;

    EXPECT_TRUE(EqualTo<std::remove_cvref_t<decltype(lhs.get_objects())>> {}(lhs.get_objects(), rhs.get_objects()));
    EXPECT_FALSE(EqualTo<std::remove_cvref_t<decltype(lhs.get_objects())>> {}(lhs.get_objects(), other.get_objects()));
    EXPECT_EQ(Hash<std::remove_cvref_t<decltype(lhs.get_objects())>> {}(lhs.get_objects()),
              Hash<std::remove_cvref_t<decltype(rhs.get_objects())>> {}(rhs.get_objects()));
    EXPECT_TRUE(Less<std::remove_cvref_t<decltype(lhs.get_objects())>> {}(lhs.get_objects(), other.get_objects()));
}

}
}
