/*
 * Copyright (C) 2025-2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_TESTS_FORMALISM_SYMBOL_CONCEPTS_HPP_
#define YGG_TESTS_FORMALISM_SYMBOL_CONCEPTS_HPP_

#include "yggdrasil/formalism/declarations.hpp"

#include <concepts>
#include <type_traits>

namespace ygg::tests
{

/// Read access to one supported symbol representation.
template<typename Repository, typename T>
concept SymbolRepositoryFor = formalism::SupportsSymbol<Repository, T> && requires(const std::remove_reference_t<Repository>& repository, urd::index<T> index) {
    { repository[index] } -> std::same_as<const urd::data<T>&>;
};

/// A context whose repository reads T's records and is numbered (layer()).
template<typename C, typename T>
concept SymbolContextFor = requires(const C& context) {
    { get_repository(context) } -> SymbolRepositoryFor<T>;
    get_repository(context).layer();
};

}  // namespace ygg::tests

#endif
