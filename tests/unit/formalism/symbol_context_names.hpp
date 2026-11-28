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

#ifndef YGG_TESTS_FORMALISM_SYMBOL_CONTEXT_NAMES_HPP_
#define YGG_TESTS_FORMALISM_SYMBOL_CONTEXT_NAMES_HPP_

// The repository the symbol-context test instantiates with YGG_FORMALISM_SYMBOL_CONTEXT, declared in a header as a
// library declares its repositories: explicit instantiations are written for other sources to link to, so the types
// they name have external linkage (of internal linkage, every member a source does not call is defined but unused).

#include "yggdrasil/formalism/symbol_context.hpp"

#include <boost/mp11.hpp>
#include <cstddef>
#include <type_traits>
#include <urd/elements/flat.hpp>
#include <urd/elements/view.hpp>

namespace ygg::tests::symbol_context
{
struct Name
{
};
}  // namespace ygg::tests::symbol_context

template<>
struct urd::data<ygg::tests::symbol_context::Name>
{
    urd::key<ygg::tests::symbol_context::Name> index;
    int value = 0;

    data() = default;

    explicit data(int value_) : index(), value(value_) {}

    static constexpr bool copyable() noexcept
    {
        static_assert(std::is_trivially_copyable_v<data>, "a copyable record is trivially copyable");
        return true;
    }

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"value", &data::value>> {}; }

    static constexpr auto cista_members() noexcept { return urd::cista_members_of<data>(); }

    [[nodiscard]] urd::key<ygg::tests::symbol_context::Name> key() const noexcept { return urd::key<ygg::tests::symbol_context::Name>(index); }
};

template<typename V>
struct urd::view_mixin<ygg::tests::symbol_context::Name, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] decltype(auto) value() const { return self().template member<&urd::record_of<V>::value>(); }
};

namespace ygg::tests::symbol_context
{
class Names : public ygg::formalism::SymbolContext<Names, boost::mp11::mp_list<Name>>
{
    using Base = ygg::formalism::SymbolContext<Names, boost::mp11::mp_list<Name>>;

public:
    Names() = default;

    explicit Names(const Names* parent) : Base(parent) {}

    [[nodiscard]] std::size_t layer() const noexcept { return 0; }
};
}  // namespace ygg::tests::symbol_context

#endif
