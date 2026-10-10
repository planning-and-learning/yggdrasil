/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_QUERY_VIEW_HPP_
#define YGG_DATABASE_QUERY_VIEW_HPP_

#include "yggdrasil/containers/variant.hpp"
#include "yggdrasil/database/query_data.hpp"

#include <concepts>
#include <span>
#include <tuple>

namespace ygg
{
template<database::ColumnTypes Values, typename Tag>
class View<Index<database::Query<Values, Tag>>, database::QueryRepository<Values>>
{
    Index<database::Query<Values, Tag>> m_index;
    const database::QueryRepository<Values>* m_repository;

public:
    using tag_type = Tag;

    View(Index<database::Query<Values, Tag>> index, const database::QueryRepository<Values>& repository) : m_index(index), m_repository(&repository) {}
    auto get_index() const noexcept { return m_index; }
    const auto& get_handle() const noexcept { return m_index; }
    const auto& get_context() const noexcept { return *m_repository; }
    const auto& get_repository() const noexcept { return *m_repository; }
    const auto& get_data() const { return (*m_repository)[m_index]; }

    auto get_variant() const
        requires std::same_as<Tag, void>
    {
        return make_view(get_data().variant, *m_repository);
    }
    std::span<const database::ColumnLayout> columns() const
    {
        if constexpr (std::same_as<Tag, void>)
            return ygg::visit([](auto concrete) { return concrete.columns(); }, get_variant());
        else if constexpr (std::same_as<Tag, database::QueryJoinTag> || std::same_as<Tag, database::QueryProjectTag>
                           || std::same_as<Tag, database::QueryDistanceTag> || std::same_as<Tag, database::QueryGenericJoinTag>)
            return get_plan().output_columns().span();
        else
            return { get_data().columns.data(), get_data().columns.size() };
    }
    auto get_arg() const
        requires(std::same_as<Tag, database::QueryProjectTag> || std::same_as<Tag, database::QueryRenameTag> || std::same_as<Tag, database::QuerySelectEqualTag>
                 || std::same_as<Tag, database::QuerySelectValueTag>)
    {
        return make_view(get_data().arg, *m_repository);
    }
    auto get_lhs() const
        requires(std::same_as<Tag, database::QueryJoinTag> || std::same_as<Tag, database::QueryUnionTag> || std::same_as<Tag, database::QueryDifferenceTag>)
    {
        return make_view(get_data().lhs, *m_repository);
    }
    auto get_rhs() const
        requires(std::same_as<Tag, database::QueryJoinTag> || std::same_as<Tag, database::QueryUnionTag> || std::same_as<Tag, database::QueryDifferenceTag>)
    {
        return make_view(get_data().rhs, *m_repository);
    }
    auto get_sources() const
        requires std::same_as<Tag, database::QueryDistanceTag>
    {
        return make_view(get_data().sources, *m_repository);
    }
    auto get_edges() const
        requires std::same_as<Tag, database::QueryDistanceTag>
    {
        return make_view(get_data().edges, *m_repository);
    }
    auto get_targets() const
        requires std::same_as<Tag, database::QueryDistanceTag>
    {
        return make_view(get_data().targets, *m_repository);
    }
    auto get_input_slot() const
        requires std::same_as<Tag, database::QueryInputTag>
    {
        return get_data().input_slot;
    }
    auto get_labels() const
        requires(std::same_as<Tag, database::QueryProjectTag> || std::same_as<Tag, database::QueryRenameTag>)
    {
        return std::span<const Index<database::Column>>(get_data().labels.data(), get_data().labels.size());
    }
    auto get_lhs_column() const
        requires std::same_as<Tag, database::QuerySelectEqualTag>
    {
        return get_data().lhs_column;
    }
    auto get_rhs_column() const
        requires std::same_as<Tag, database::QuerySelectEqualTag>
    {
        return get_data().rhs_column;
    }
    auto get_column() const
        requires std::same_as<Tag, database::QuerySelectValueTag>
    {
        return get_data().column;
    }
    auto get_constant() const
        requires std::same_as<Tag, database::QuerySelectValueTag>
    {
        return std::span<const std::byte>(get_data().constant.data(), get_data().constant.size());
    }
    auto get_distance_column() const
        requires std::same_as<Tag, database::QueryDistanceTag>
    {
        return get_data().distance_column;
    }
    const auto& get_plan() const
        requires(std::same_as<Tag, database::QueryJoinTag> || std::same_as<Tag, database::QueryProjectTag> || std::same_as<Tag, database::QueryDistanceTag>
                 || std::same_as<Tag, database::QueryGenericJoinTag>)
    {
        return get_data().plan;
    }
    size_t get_lhs_position() const
        requires std::same_as<Tag, database::QuerySelectEqualTag>
    {
        return get_data().lhs_position;
    }
    size_t get_rhs_position() const
        requires std::same_as<Tag, database::QuerySelectEqualTag>
    {
        return get_data().rhs_position;
    }
    size_t get_position() const
        requires std::same_as<Tag, database::QuerySelectValueTag>
    {
        return get_data().column_position;
    }
    auto get_inputs() const
        requires std::same_as<Tag, database::QueryGenericJoinTag>
    {
        return std::span<const Index<database::Query<Values>>>(get_data().inputs.data(), get_data().inputs.size());
    }
    auto get_variable_order() const
        requires std::same_as<Tag, database::QueryGenericJoinTag>
    {
        return std::span<const Index<database::Column>>(get_data().variable_order.data(), get_data().variable_order.size());
    }
    auto get_output_order() const
        requires std::same_as<Tag, database::QueryGenericJoinTag>
    {
        return std::span<const Index<database::Column>>(get_data().output_order.data(), get_data().output_order.size());
    }
    auto identifying_members() const noexcept { return std::make_tuple(m_index, m_repository->get_index()); }
};
}  // namespace ygg

namespace ygg::database
{
template<ColumnTypes Values, typename Tag, typename F>
void for_each_child(QueryView<Values, Tag> view, F&& f)
{
    if constexpr (std::same_as<Tag, void>)
        ygg::visit([&](auto concrete) { for_each_child(concrete, f); }, view.get_variant());
    else
        for_each_child(view.get_data(), [&](Index<Query<Values>> child) { f(QueryView<Values>(child, view.get_repository())); });
}
}  // namespace ygg::database

#endif
