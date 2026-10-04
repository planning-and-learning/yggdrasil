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

#include "yggdrasil/core/concepts.hpp"
#include "yggdrasil/core/types.hpp"

#include <concepts>
#include <type_traits>

namespace ygg::formalism
{

/// Whether a repository's symbol inventory contains T.
template<typename Repository, typename T>
concept SupportsSymbol = std::remove_cvref_t<Repository>::SymbolTypes::template contains<T>;

/// Whether a repository's relation inventory contains a relation tag.
template<typename Repository, typename T>
concept SupportsRelation = std::remove_cvref_t<Repository>::RelationTypes::template contains<T>;

/// Read access to one supported symbol representation.
template<typename Repository, typename T>
concept SymbolRepositoryFor = SupportsSymbol<Repository, T> && requires(const std::remove_reference_t<Repository>& repository, Index<T> index) {
    { repository[index] } -> std::same_as<const Data<T>&>;
};

template<typename C, typename T>
concept SymbolContextFor = requires(const C& context) {
    { get_repository(context) } -> SymbolRepositoryFor<T>;
    get_repository(context).get_index();
};

template<typename Tag>
struct Object
{
};

struct Row
{
};

template<typename RelationTag_, typename ObjectTag_>
struct RelationBinding
{
    using relation_tag = RelationTag_;
    using object_tag = ObjectTag_;
};

template<typename T>
struct is_relation_binding : std::false_type
{
};

template<typename RelationTag, typename ObjectTag>
struct is_relation_binding<RelationBinding<RelationTag, ObjectTag>> : std::true_type
{
};

template<typename T>
inline constexpr bool is_relation_binding_v = is_relation_binding<std::remove_cvref_t<T>>::value;

template<typename T>
concept RelationBindingConcept = is_relation_binding_v<T>;

template<typename T>
concept NonRelationBindingConcept = !RelationBindingConcept<T>;

/// Read access to a supported binding's row representation.
template<typename Repository, typename Binding>
concept RelationRepositoryFor = RelationBindingConcept<Binding> && SupportsRelation<Repository, typename Binding::relation_tag>
                                && std::same_as<typename Binding::object_tag, typename std::remove_cvref_t<Repository>::object_tag>
                                && requires(const std::remove_reference_t<Repository>& repository, Index<Binding> index) {
                                       { repository[index] } -> InputRangeOf<Index<Object<typename Binding::object_tag>>>;
                                   };

template<typename C, typename Binding>
concept RelationContextFor = requires(const C& context) {
    { get_repository(context) } -> RelationRepositoryFor<Binding>;
};

}  // namespace ygg::formalism

#endif
