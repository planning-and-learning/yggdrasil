/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_SYNTAX_QUERY_VIEW_HPP_
#define YGG_DATABASE_SYNTAX_QUERY_VIEW_HPP_

#include <urd/elements/flat.hpp>
#include <boost/mp11.hpp>
#include "yggdrasil/database/syntax/query_data.hpp"

#include <concepts>
#include <span>
#include <tuple>
#include <urd/elements/children.hpp>

namespace urd
{
template<typename Tag, typename V>
struct view_mixin<ygg::database::Query<Tag>, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] auto get_variant() const
        requires std::same_as<Tag, void>
    { return urd::resolve(self().data().variant, self().context()); }
    [[nodiscard]] std::span<const ygg::database::ColumnLayout> columns() const
    {
        if constexpr (std::same_as<Tag, void>)
            return std::visit([](auto concrete) { return concrete.columns(); }, get_variant());
        else if constexpr (std::same_as<Tag, ygg::database::QueryJoinTag> || std::same_as<Tag, ygg::database::QueryProjectTag>
                           || std::same_as<Tag, ygg::database::QueryDistanceTag> || std::same_as<Tag, ygg::database::QueryGenericJoinTag>)
            return get_plan().output_columns().span();
        else if constexpr (std::same_as<Tag, ygg::database::QuerySemijoinTag> || std::same_as<Tag, ygg::database::QueryAntijoinTag>)
            return get_plan().lhs_columns().span();
        else
            return { self().data().columns.data(), self().data().columns.size() };
    }
    [[nodiscard]] auto get_arg() const
        requires(std::same_as<Tag, ygg::database::QueryProjectTag> || std::same_as<Tag, ygg::database::QueryRenameTag> || std::same_as<Tag, ygg::database::QuerySelectEqualTag>
                 || std::same_as<Tag, ygg::database::QuerySelectValueTag>)
    { return urd::resolve(self().data().arg, self().context()); }
    [[nodiscard]] auto get_lhs() const
        requires(std::same_as<Tag, ygg::database::QueryJoinTag> || std::same_as<Tag, ygg::database::QueryUnionTag> || std::same_as<Tag, ygg::database::QueryDifferenceTag>
                 || std::same_as<Tag, ygg::database::QuerySemijoinTag> || std::same_as<Tag, ygg::database::QueryAntijoinTag>)
    { return urd::resolve(self().data().lhs, self().context()); }
    [[nodiscard]] auto get_rhs() const
        requires(std::same_as<Tag, ygg::database::QueryJoinTag> || std::same_as<Tag, ygg::database::QueryUnionTag> || std::same_as<Tag, ygg::database::QueryDifferenceTag>
                 || std::same_as<Tag, ygg::database::QuerySemijoinTag> || std::same_as<Tag, ygg::database::QueryAntijoinTag>)
    { return urd::resolve(self().data().rhs, self().context()); }
    [[nodiscard]] auto get_sources() const
        requires std::same_as<Tag, ygg::database::QueryDistanceTag>
    { return urd::resolve(self().data().sources, self().context()); }
    [[nodiscard]] auto get_edges() const
        requires std::same_as<Tag, ygg::database::QueryDistanceTag>
    { return urd::resolve(self().data().edges, self().context()); }
    [[nodiscard]] auto get_targets() const
        requires std::same_as<Tag, ygg::database::QueryDistanceTag>
    { return urd::resolve(self().data().targets, self().context()); }
    [[nodiscard]] auto get_input_slot() const
        requires std::same_as<Tag, ygg::database::QueryInputTag>
    { return self().data().input_slot; }
    [[nodiscard]] auto get_labels() const
        requires(std::same_as<Tag, ygg::database::QueryProjectTag> || std::same_as<Tag, ygg::database::QueryRenameTag>)
    { return std::span<const urd::index<ygg::database::Column>>(self().data().labels.data(), self().data().labels.size()); }
    [[nodiscard]] auto get_lhs_column() const
        requires std::same_as<Tag, ygg::database::QuerySelectEqualTag>
    { return self().data().lhs_column; }
    [[nodiscard]] auto get_rhs_column() const
        requires std::same_as<Tag, ygg::database::QuerySelectEqualTag>
    { return self().data().rhs_column; }
    [[nodiscard]] auto get_column() const
        requires std::same_as<Tag, ygg::database::QuerySelectValueTag>
    { return self().data().column; }
    [[nodiscard]] auto get_constant() const
        requires std::same_as<Tag, ygg::database::QuerySelectValueTag>
    { return std::span<const std::byte>(self().data().constant.data(), self().data().constant.size()); }
    [[nodiscard]] auto get_distance_column() const
        requires std::same_as<Tag, ygg::database::QueryDistanceTag>
    { return self().data().distance_column; }
    [[nodiscard]] const auto& get_plan() const
        requires(std::same_as<Tag, ygg::database::QueryJoinTag> || std::same_as<Tag, ygg::database::QueryProjectTag> || std::same_as<Tag, ygg::database::QueryDistanceTag>
                 || std::same_as<Tag, ygg::database::QueryGenericJoinTag> || std::same_as<Tag, ygg::database::QuerySemijoinTag>
                 || std::same_as<Tag, ygg::database::QueryAntijoinTag>)
    { return self().data().plan; }
    [[nodiscard]] size_t get_lhs_position() const
        requires std::same_as<Tag, ygg::database::QuerySelectEqualTag>
    { return self().data().lhs_position; }
    [[nodiscard]] size_t get_rhs_position() const
        requires std::same_as<Tag, ygg::database::QuerySelectEqualTag>
    { return self().data().rhs_position; }
    [[nodiscard]] size_t get_position() const
        requires std::same_as<Tag, ygg::database::QuerySelectValueTag>
    { return self().data().column_position; }
    [[nodiscard]] auto get_inputs() const
        requires std::same_as<Tag, ygg::database::QueryGenericJoinTag>
    { return std::span<const urd::index<ygg::database::Query<>>>(self().data().inputs.data(), self().data().inputs.size()); }
    [[nodiscard]] auto get_variable_order() const
        requires std::same_as<Tag, ygg::database::QueryGenericJoinTag>
    { return std::span<const urd::index<ygg::database::Column>>(self().data().variable_order.data(), self().data().variable_order.size()); }
    [[nodiscard]] auto get_output_order() const
        requires std::same_as<Tag, ygg::database::QueryGenericJoinTag>
    { return std::span<const urd::index<ygg::database::Column>>(self().data().output_order.data(), self().data().output_order.size()); }
};
}  // namespace urd

namespace ygg::database
{
/// Calls f(QueryView<>) for every operand of a query, in operand order.
template<typename Tag, std::invocable<QueryView<>> F>
void for_each_child(QueryView<Tag> view, F f)
{
    if constexpr (std::same_as<Tag, void>)
        std::visit([&](auto concrete) { for_each_child(concrete, f); }, view.get_variant());
    else
        urd::for_each_child<boost::mp11::mp_list<Query<>>, Query<Tag>>(view.data(), view.context(), f);
}
}  // namespace ygg::database

#endif
