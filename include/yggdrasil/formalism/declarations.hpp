/*
 * Copyright (C) 2025-2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_FORMALISM_DECLARATIONS_HPP_
#define YGG_FORMALISM_DECLARATIONS_HPP_

#include <type_traits>

namespace ygg::formalism
{

template<typename Tag>
struct Object
{
};

struct Row
{
};

template<typename Relation, typename ObjectTag>
struct RelationBinding
{
};

template<typename T>
struct is_relation_binding : std::false_type
{
};

template<typename Relation, typename ObjectTag>
struct is_relation_binding<RelationBinding<Relation, ObjectTag>> : std::true_type
{
};

template<typename T>
inline constexpr bool is_relation_binding_v = is_relation_binding<std::remove_cvref_t<T>>::value;

template<typename T>
concept RelationBindingConcept = is_relation_binding_v<T>;

template<typename T>
concept NonRelationBindingConcept = !RelationBindingConcept<T>;

}

#endif
