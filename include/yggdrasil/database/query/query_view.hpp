/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_QUERY_QUERY_VIEW_HPP_
#define YGG_DATABASE_QUERY_QUERY_VIEW_HPP_

#include "yggdrasil/containers/variant.hpp"
#include "yggdrasil/core/children.hpp"
#include "yggdrasil/database/query/query_data.hpp"

#include <concepts>
#include <span>
#include <tuple>

namespace ygg
{
template<typename Tag>
class View<Index<database::Query<Tag>>, database::QueryRepository> : public IndexViewBase<database::Query<Tag>, database::QueryRepository>
{
public:
    using IndexViewBase<database::Query<Tag>, database::QueryRepository>::IndexViewBase;

    [[nodiscard]] auto get_variant() const
        requires std::same_as<Tag, void>
    { return make_view(this->get_data().variant, this->get_context()); }
    [[nodiscard]] std::span<const database::ColumnLayout> columns() const
    {
        if constexpr (std::same_as<Tag, void>)
            return ygg::visit([](auto concrete) { return concrete.columns(); }, get_variant());
        else if constexpr (std::same_as<Tag, database::QueryJoinTag> || std::same_as<Tag, database::QueryProjectTag>
                           || std::same_as<Tag, database::QueryDistanceTag> || std::same_as<Tag, database::QueryGenericJoinTag>)
            return get_plan().output_columns().span();
        else if constexpr (std::same_as<Tag, database::QuerySemijoinTag> || std::same_as<Tag, database::QueryAntijoinTag>)
            return get_plan().lhs_columns().span();
        else
            return { this->get_data().columns.data(), this->get_data().columns.size() };
    }
    [[nodiscard]] auto get_arg() const
        requires(std::same_as<Tag, database::QueryProjectTag> || std::same_as<Tag, database::QueryRenameTag> || std::same_as<Tag, database::QuerySelectEqualTag>
                 || std::same_as<Tag, database::QuerySelectValueTag>)
    { return make_view(this->get_data().arg, this->get_context()); }
    [[nodiscard]] auto get_lhs() const
        requires(std::same_as<Tag, database::QueryJoinTag> || std::same_as<Tag, database::QueryUnionTag> || std::same_as<Tag, database::QueryDifferenceTag>
                 || std::same_as<Tag, database::QuerySemijoinTag> || std::same_as<Tag, database::QueryAntijoinTag>)
    { return make_view(this->get_data().lhs, this->get_context()); }
    [[nodiscard]] auto get_rhs() const
        requires(std::same_as<Tag, database::QueryJoinTag> || std::same_as<Tag, database::QueryUnionTag> || std::same_as<Tag, database::QueryDifferenceTag>
                 || std::same_as<Tag, database::QuerySemijoinTag> || std::same_as<Tag, database::QueryAntijoinTag>)
    { return make_view(this->get_data().rhs, this->get_context()); }
    [[nodiscard]] auto get_sources() const
        requires std::same_as<Tag, database::QueryDistanceTag>
    { return make_view(this->get_data().sources, this->get_context()); }
    [[nodiscard]] auto get_edges() const
        requires std::same_as<Tag, database::QueryDistanceTag>
    { return make_view(this->get_data().edges, this->get_context()); }
    [[nodiscard]] auto get_targets() const
        requires std::same_as<Tag, database::QueryDistanceTag>
    { return make_view(this->get_data().targets, this->get_context()); }
    [[nodiscard]] auto get_input_slot() const
        requires std::same_as<Tag, database::QueryInputTag>
    { return this->get_data().input_slot; }
    [[nodiscard]] auto get_labels() const
        requires(std::same_as<Tag, database::QueryProjectTag> || std::same_as<Tag, database::QueryRenameTag>)
    { return std::span<const Index<database::Column>>(this->get_data().labels.data(), this->get_data().labels.size()); }
    [[nodiscard]] auto get_lhs_column() const
        requires std::same_as<Tag, database::QuerySelectEqualTag>
    { return this->get_data().lhs_column; }
    [[nodiscard]] auto get_rhs_column() const
        requires std::same_as<Tag, database::QuerySelectEqualTag>
    { return this->get_data().rhs_column; }
    [[nodiscard]] auto get_column() const
        requires std::same_as<Tag, database::QuerySelectValueTag>
    { return this->get_data().column; }
    [[nodiscard]] auto get_constant() const
        requires std::same_as<Tag, database::QuerySelectValueTag>
    { return std::span<const std::byte>(this->get_data().constant.data(), this->get_data().constant.size()); }
    [[nodiscard]] auto get_distance_column() const
        requires std::same_as<Tag, database::QueryDistanceTag>
    { return this->get_data().distance_column; }
    [[nodiscard]] const auto& get_plan() const
        requires(std::same_as<Tag, database::QueryJoinTag> || std::same_as<Tag, database::QueryProjectTag> || std::same_as<Tag, database::QueryDistanceTag>
                 || std::same_as<Tag, database::QueryGenericJoinTag> || std::same_as<Tag, database::QuerySemijoinTag>
                 || std::same_as<Tag, database::QueryAntijoinTag>)
    { return this->get_data().plan; }
    [[nodiscard]] size_t get_lhs_position() const
        requires std::same_as<Tag, database::QuerySelectEqualTag>
    { return this->get_data().lhs_position; }
    [[nodiscard]] size_t get_rhs_position() const
        requires std::same_as<Tag, database::QuerySelectEqualTag>
    { return this->get_data().rhs_position; }
    [[nodiscard]] size_t get_position() const
        requires std::same_as<Tag, database::QuerySelectValueTag>
    { return this->get_data().column_position; }
    [[nodiscard]] auto get_inputs() const
        requires std::same_as<Tag, database::QueryGenericJoinTag>
    { return std::span<const Index<database::Query<>>>(this->get_data().inputs.data(), this->get_data().inputs.size()); }
    [[nodiscard]] auto get_variable_order() const
        requires std::same_as<Tag, database::QueryGenericJoinTag>
    { return std::span<const Index<database::Column>>(this->get_data().variable_order.data(), this->get_data().variable_order.size()); }
    [[nodiscard]] auto get_output_order() const
        requires std::same_as<Tag, database::QueryGenericJoinTag>
    { return std::span<const Index<database::Column>>(this->get_data().output_order.data(), this->get_data().output_order.size()); }
};
}  // namespace ygg

namespace ygg::database
{
/// Calls f(QueryView<>) for every operand of a query, in operand order.
template<typename Tag, std::invocable<QueryView<>> F>
void for_each_child(QueryView<Tag> view, F f)
{
    if constexpr (std::same_as<Tag, void>)
        ygg::visit([&](auto concrete) { for_each_child(concrete, f); }, view.get_variant());
    else
        ygg::for_each_child<TypeList<Query<>>>(view, f);
}
}  // namespace ygg::database

#endif
