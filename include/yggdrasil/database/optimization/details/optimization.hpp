/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_OPTIMIZATION_DETAILS_OPTIMIZATION_HPP_
#define YGG_DATABASE_OPTIMIZATION_DETAILS_OPTIMIZATION_HPP_

#include "yggdrasil/database/syntax/query.hpp"

#include <array>
#include <bit>
#include <limits>
#include <cassert>
#include <compare>
#include <concepts>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace ygg::database::optimization_detail
{
template<ColumnTypes Values>
struct MemoGroup;
}
namespace ygg
{
template<database::ColumnTypes Values>
struct Index<database::optimization_detail::MemoGroup<Values>> : IndexMixin<Index<database::optimization_detail::MemoGroup<Values>>>
{
    using IndexMixin<Index<database::optimization_detail::MemoGroup<Values>>>::IndexMixin;
};
}

namespace ygg::database::optimization_detail
{
template<ColumnTypes Values>
using MemoGroupId = Index<MemoGroup<Values>>;
struct InputFactorIdentity
{
    size_t value;
    auto operator<=>(const InputFactorIdentity&) const = default;
};
struct MemoFactorIdentity
{
    size_t value;
    auto operator<=>(const MemoFactorIdentity&) const = default;
};
struct PlanFactorIdentity
{
    size_t value;
    auto operator<=>(const PlanFactorIdentity&) const = default;
};
using FactorIdentity = std::variant<InputFactorIdentity, MemoFactorIdentity, PlanFactorIdentity>;
struct Factor
{
    FactorIdentity identity;
    double rows;
    std::vector<std::tuple<size_t, uint_t, double>> attributes;
};
struct Estimate
{
    double rows = 0;
    std::map<Index<Column>, double> distinct;
    std::vector<Factor> factors;
    std::set<std::pair<uint_t, uint_t>> equalities;
    std::set<std::pair<uint_t, std::vector<std::byte>>> constants;
};
/// Optimizer state of one memo expression, indexed by its query index.
template<ColumnTypes Values>
struct ExpressionAnnotation
{
    MemoGroupId<Values> group;
    std::optional<Index<Query<Values>>> source;
    Estimate estimate;
    std::optional<Index<Query<Values>>> baseline;
};
/// Work and retained cells of one physical operator, excluding its inputs.
struct OwnCost
{
    double work = 0, retained = 0;
};
/// Optimizer state of one physical candidate, indexed by its query index.
template<ColumnTypes Values>
struct Candidate
{
    std::optional<Index<Query<Values>>> source;
    MemoGroupId<Values> group;
    double rows = 0;
    OwnCost own;
    /// work + memory_weight * retained over the candidate's distinct operators.
    double score = 0;
    /// Sorted groups of the candidate and its descendants, to reject cycles.
    std::vector<MemoGroupId<Values>> groups;
};
template<class Tag>
concept FilterTag = std::same_as<Tag, QuerySelectEqualTag> || std::same_as<Tag, QuerySelectValueTag>;
inline bool has(std::span<const ColumnLayout> columns, Index<Column> label)
{
    return std::ranges::any_of(columns, [&](const auto& c) { return c.label == label; });
}
inline bool same_set(std::span<const ColumnLayout> a, std::span<const ColumnLayout> b)
{
    return a.size() == b.size()
           && std::ranges::all_of(a,
                                  [&](const auto& c) { return std::ranges::any_of(b, [&](const auto& d) { return c.label == d.label && c.type == d.type; }); });
}
inline bool same_columns(std::span<const ColumnLayout> a, std::span<const ColumnLayout> b) { return std::ranges::equal(a, b); }
inline std::vector<ColumnLayout> own_columns(std::span<const ColumnLayout> columns) { return { columns.begin(), columns.end() }; }
/// Estimates saturate here instead of overflowing to infinity.
constexpr double max_estimate = 1e300;
/// exp() of larger logarithms overflows a double.
constexpr double max_log_estimate = 690;
/// verdog: a retained row holds its cells plus a hash-entry proxy of two cells.
constexpr double retained_overhead_cells = 2;
inline double bounded(double value) { return std::clamp(value, 0.0, max_estimate); }
inline double multiply(double a, double b) { return a == 0 || b == 0 ? 0 : bounded(a * b); }

template<class Tag, ColumnTypes Values>
const Data<Query<Values, Tag>>* query_data_if(QueryView<Values> query)
{
    return ygg::visit(
        []<typename Child>(Child child) -> const Data<Query<Values, Tag>>*
        {
            if constexpr (std::same_as<Child, QueryView<Values, Tag>>)
                return &child.get_data();
            else
                return nullptr;
        },
        query.get_variant());
}
template<ColumnTypes Values>
struct MemoGroup
{
    MemoGroupId<Values> parent;
    Index<Query<Values>> representative;
    std::vector<Index<Query<Values>>> members;
    Estimate estimate;
    std::vector<Index<Query<Values>>> frontier;
};

/// Interned queries are finite witnesses; only their equivalence groups may cycle.
template<ColumnTypes Values>
class Memo
{
public:
    using ExpressionId = Index<Query<Values>>;
    using GroupId = MemoGroupId<Values>;
    template<class Tag>
    using Operation = Data<Query<Values, Tag>>;
    /// A rewrite's expression, or nothing once the expression budget is spent.
    using Result = std::optional<ExpressionId>;

private:
    QueryRepository<Values> m_queries;
    QueryBuilder<Values> m_builder;
    std::vector<ExpressionAnnotation<Values>> m_expressions;
    std::vector<MemoGroup<Values>> m_groups;
    std::unordered_map<size_t, std::vector<ExpressionId>> m_indices;
    const SearchLimits* m_limits;
    OptimizationReport* m_report;
    /// (memo expression, caller query index) for every query reachable from the caller's roots.
    std::vector<std::pair<ExpressionId, ExpressionId>> m_originals;
    std::vector<ExpressionId> m_roots;
    bool m_changed = false;

    template<class T>
    const T& canonical_member(const T& value) const
    {
        return value;
    }
    ExpressionId canonical_member(ExpressionId child) const { return representative(group(child)); }
    std::vector<ExpressionId> canonical_member(const cista::offset::vector<ExpressionId>& children) const
    {
        std::vector<ExpressionId> result;
        for (const auto child : children)
            result.push_back(canonical_member(child));
        return result;
    }
    template<class Tag>
    auto identity(const Operation<Tag>& data) const
    {
        return std::apply([&](const auto&... member) { return std::tuple<decltype(canonical_member(member))...> { canonical_member(member)... }; },
                          data.identifying_members());
    }
    template<class Tag>
    size_t key(const Operation<Tag>& data) const
    {
        typename Data<Query<Values>>::Variant tag = Index<Query<Values, Tag>>(0);
        return ygg::hash_combine(tag.index(), identity(data));
    }
    template<class Tag>
    bool matches(ExpressionId candidate, const Operation<Tag>& data) const
    {
        const auto* existing = query_data_if<Tag>(node(candidate));
        return existing && EqualTo<decltype(identity(data))> {}(identity(*existing), identity(data));
    }
    /// Interns an expression in its own group; nothing once the budget is spent.
    template<class Tag>
    Result insert(Operation<Tag>& data, std::optional<ExpressionId> source = {}, bool original = false)
    {
        detail::query_children<Tag>(data, [&](ExpressionId& child) { child = representative(group(child)); });
        const auto hash = key(data);
        for (const auto candidate : m_indices[hash])
            if (matches(candidate, data))
                return candidate;
        if (!original && size() >= m_limits->memo_expressions)
        {
            m_report->budget_exhausted = true;
            return std::nullopt;
        }
        const auto old_size = size();
        const auto id = insert_query(m_queries, m_builder, data).get_index();
        if (size() != old_size)
        {
            if (m_groups.size() >= GroupId::MAX)
                throw std::overflow_error("Optimizer: group index space exhausted.");
            const auto group_id = GroupId(to_uint_t(m_groups.size()));
            assert(id.get_value() == m_expressions.size());
            m_expressions.push_back({ group_id, source, {}, {} });
            m_groups.push_back({ group_id, id, { id }, {}, {} });
            m_changed = true;
        }
        m_indices[hash].push_back(id);
        return id;
    }
    void merge(ExpressionId lhs, ExpressionId rhs)
    {
        auto left = group(lhs), right = group(rhs);
        if (left == right)
            return;
        assert(same_columns(node(lhs).columns(), node(rhs).columns()) && "Equivalence groups preserve ordered schemas.");
        if (representative(right) < representative(left))
            std::swap(left, right);
        auto& into = m_groups[left.get_value()];
        auto& from = m_groups[right.get_value()];
        from.parent = left;
        into.members.insert(into.members.end(), from.members.begin(), from.members.end());
        from.members.clear();
        m_changed = true;
    }
    /// Merges the groups after aligning the alternative to the expression's ordered schema.
    void equate(ExpressionId expression, Result alternative)
    {
        if (const auto aligned = project(alternative, node(expression).columns()))
            merge(expression, *aligned);
    }
    bool equal(ExpressionId lhs, ExpressionId rhs) const { return group(lhs) == group(rhs); }
    bool fits(const Operation<QuerySelectEqualTag>& filter, ExpressionId child) const
    {
        return has(node(child).columns(), filter.lhs_column) && has(node(child).columns(), filter.rhs_column);
    }
    bool fits(const Operation<QuerySelectValueTag>& filter, ExpressionId child) const { return has(node(child).columns(), filter.column); }
    template<class Tag>
    Result unary(const Operation<Tag>& operation, Result child)
    {
        if (!child)
            return std::nullopt;
        auto data = checkout<Query<Values, Tag>>(m_builder);
        *data = operation;
        data->arg = *child;
        return insert(*data);
    }
    template<class Tag>
    Result binary(Result lhs, Result rhs)
    {
        if (!lhs || !rhs)
            return std::nullopt;
        auto data = checkout<Query<Values, Tag>>(m_builder);
        data->lhs = *lhs;
        data->rhs = *rhs;
        return insert(*data);
    }
    Result empty(std::span<const ColumnLayout> columns)
    {
        auto data = checkout<Query<Values, QueryEmptyTag>>(m_builder);
        data->columns.set(columns.begin(), columns.end());
        return insert(*data);
    }
    Result project(Result expression, std::span<const ColumnLayout> columns)
    {
        if (!expression)
            return std::nullopt;
        auto child = representative(group(*expression));
        if (same_columns(node(child).columns(), columns))
            return child;
        while (const auto* projection = query_data_if<QueryProjectTag>(node(child)))
            child = projection->arg;
        if (same_columns(node(child).columns(), columns))
            return child;
        auto data = checkout<Query<Values, QueryProjectTag>>(m_builder);
        data->arg = child;
        const auto labels = detail::query_labels(columns);
        data->labels.set(labels.begin(), labels.end());
        return insert(*data);
    }
    Result rename(Result child, std::span<const ColumnLayout> columns)
    {
        if (!child || same_columns(node(*child).columns(), columns))
            return child;
        auto data = checkout<Query<Values, QueryRenameTag>>(m_builder);
        data->arg = *child;
        const auto labels = detail::query_labels(columns);
        data->labels.set(labels.begin(), labels.end());
        return insert(*data);
    }
    void rewrite(ExpressionId id, QueryView<Values, QueryProjectTag> query)
    {
        const auto& data = query.get_data();
        const auto columns = own_columns(node(id).columns());
        if (same_columns(node(data.arg).columns(), columns))
            equate(id, data.arg);
        for (const auto form : forms(data.arg))
        {
            if (m_report->budget_exhausted)
                return;
            const auto expression = node(form);
            ygg::visit(
                [&]<typename Child>(Child child)
                {
                    const auto& inner = child.get_data();
                    if constexpr (std::same_as<Child, QueryView<Values, QueryEmptyTag>>)
                        equate(id, empty(columns));
                    else if constexpr (std::same_as<Child, QueryView<Values, QueryProjectTag>>)
                        equate(id, project(inner.arg, columns));
                    else if constexpr (std::same_as<Child, QueryView<Values, QueryUnionTag>>)
                        equate(id, binary<QueryUnionTag>(project(inner.lhs, columns), project(inner.rhs, columns)));
                    else if constexpr (std::same_as<Child, QueryView<Values, QueryJoinTag>>)
                    {
                        std::vector<ColumnLayout> left, right;
                        const auto lc = node(inner.lhs).columns(), rc = node(inner.rhs).columns();
                        for (const auto& c : lc)
                            if (has(columns, c.label) || has(rc, c.label))
                                left.push_back(c);
                        for (const auto& c : rc)
                            if (has(columns, c.label) || has(lc, c.label))
                                right.push_back(c);
                        equate(id, project(binary<QueryJoinTag>(project(inner.lhs, left), project(inner.rhs, right)), columns));
                    }
                    else if constexpr (std::same_as<Child, QueryView<Values, QueryRenameTag>>)
                    {
                        std::vector<ColumnLayout> inverse;
                        const auto original = node(inner.arg).columns();
                        for (const auto& c : columns)
                            inverse.push_back(original[column_index(expression.columns(), c.label)]);
                        equate(id, rename(project(inner.arg, inverse), columns));
                    }
                    else if constexpr ((std::same_as<Child, QueryView<Values, QuerySelectEqualTag>>
                                        || std::same_as<Child, QueryView<Values, QuerySelectValueTag>>) )
                    {
                        auto needed = columns;
                        const auto add = [&](Index<Column> label)
                        {
                            if (!has(needed, label))
                                needed.push_back(expression.columns()[column_index(expression.columns(), label)]);
                        };
                        if constexpr (std::same_as<Child, QueryView<Values, QuerySelectEqualTag>>)
                        {
                            add(inner.lhs_column);
                            add(inner.rhs_column);
                        }
                        else
                            add(inner.column);
                        equate(id, project(unary(inner, project(inner.arg, needed)), columns));
                    }
                },
                expression.get_variant());
        }
    }
    void rewrite(ExpressionId id, QueryView<Values, QueryRenameTag> query)
    {
        const auto& data = query.get_data();
        const auto columns = own_columns(node(id).columns());
        if (same_columns(node(data.arg).columns(), columns))
            equate(id, data.arg);
        for (const auto form : forms(data.arg))
            if (query_data_if<QueryEmptyTag>(node(form)) != nullptr)
                equate(id, empty(columns));
    }
    template<FilterTag Tag>
    void rewrite(ExpressionId id, QueryView<Values, Tag> query)
    {
        const auto& data = query.get_data();
        const auto columns = own_columns(node(id).columns());
        if constexpr (std::same_as<Tag, QuerySelectEqualTag>)
            if (data.lhs_column == data.rhs_column)
                equate(id, data.arg);
        for (const auto form : forms(data.arg))
        {
            if (m_report->budget_exhausted)
                return;
            const auto expression = node(form);
            ygg::visit(
                [&]<typename Child>(Child child)
                {
                    const auto& inner = child.get_data();
                    if constexpr (std::same_as<Child, QueryView<Values, QueryEmptyTag>>)
                        equate(id, empty(columns));
                    else if constexpr ((std::same_as<Child, QueryView<Values, QuerySelectEqualTag>>
                                        || std::same_as<Child, QueryView<Values, QuerySelectValueTag>>) )
                    {
                        bool same = false;
                        if constexpr (std::same_as<Child, QueryView<Values, Tag>>)
                        {
                            auto predicate = data;
                            predicate.arg = inner.arg;
                            same = EqualTo<Operation<Tag>> {}(predicate, inner);
                        }
                        if (same)
                            equate(id, form);
                        else
                            equate(id, unary(inner, unary(data, inner.arg)));
                    }
                    else if constexpr (std::same_as<Child, QueryView<Values, QueryProjectTag>>)
                        equate(id, project(unary(data, inner.arg), columns));
                    else if constexpr (std::same_as<Child, QueryView<Values, QueryJoinTag>>)
                    {
                        if (fits(data, inner.lhs))
                            equate(id, binary<QueryJoinTag>(unary(data, inner.lhs), inner.rhs));
                        if (fits(data, inner.rhs))
                            equate(id, binary<QueryJoinTag>(inner.lhs, unary(data, inner.rhs)));
                    }
                    else if constexpr (std::same_as<Child, QueryView<Values, QueryUnionTag>>)
                        equate(id, binary<QueryUnionTag>(unary(data, inner.lhs), unary(data, inner.rhs)));
                    else if constexpr (std::same_as<Child, QueryView<Values, QueryDifferenceTag>>)
                        equate(id, binary<QueryDifferenceTag>(unary(data, inner.lhs), unary(data, inner.rhs)));
                    else if constexpr (std::same_as<Child, QueryView<Values, QueryRenameTag>>)
                    {
                        auto mapped = data;
                        const auto old = node(inner.arg).columns();
                        const auto label = [&](Index<Column> column) { return old[column_index(expression.columns(), column)].label; };
                        if constexpr (std::same_as<Tag, QuerySelectEqualTag>)
                        {
                            mapped.lhs_column = label(data.lhs_column);
                            mapped.rhs_column = label(data.rhs_column);
                        }
                        else
                            mapped.column = label(data.column);
                        equate(id, rename(unary(mapped, inner.arg), columns));
                    }
                },
                expression.get_variant());
        }
    }
    void rewrite(ExpressionId id, QueryView<Values, QueryJoinTag> query)
    {
        const auto& data = query.get_data();
        const auto columns = own_columns(node(id).columns());
        if (equal(data.lhs, data.rhs))
            equate(id, data.lhs);
        equate(id, binary<QueryJoinTag>(data.rhs, data.lhs));
        for (const auto form : forms(data.lhs))
        {
            if (m_report->budget_exhausted)
                return;
            const auto expression = node(form);
            if (query_data_if<QueryEmptyTag>(expression) != nullptr)
                equate(id, empty(columns));
            else if (const auto* inner = query_data_if<QueryJoinTag>(expression))
                equate(id, binary<QueryJoinTag>(inner->lhs, binary<QueryJoinTag>(inner->rhs, data.rhs)));
            else if (const auto* inner = query_data_if<QueryUnionTag>(expression))
            {
                if (equal(inner->lhs, data.rhs) || equal(inner->rhs, data.rhs))
                    equate(id, data.rhs);
                equate(id, binary<QueryUnionTag>(project(binary<QueryJoinTag>(inner->lhs, data.rhs), columns), project(binary<QueryJoinTag>(inner->rhs, data.rhs), columns)));
            }
            else if (const auto* inner = query_data_if<QueryProjectTag>(expression))
            {
                if (equal(inner->arg, data.rhs))
                    equate(id, data.rhs);
                // Lifting hidden variables must not capture an outer column.
                const auto hidden = node(inner->arg).columns();
                const bool captures =
                    std::ranges::any_of(hidden, [&](const auto& c) { return !has(expression.columns(), c.label) && has(node(data.rhs).columns(), c.label); });
                if (!captures)
                    equate(id, project(binary<QueryJoinTag>(inner->arg, data.rhs), columns));
            }
        }
        for (const auto form : forms(data.rhs))
        {
            const auto expression = node(form);
            if (query_data_if<QueryEmptyTag>(expression) != nullptr)
                equate(id, empty(columns));
            else if (const auto* peer = query_data_if<QueryJoinTag>(expression))
                equate(id, binary<QueryJoinTag>(binary<QueryJoinTag>(data.lhs, peer->lhs), peer->rhs));
        }
    }
    void rewrite(ExpressionId id, QueryView<Values, QueryUnionTag> query)
    {
        const auto& data = query.get_data();
        if (equal(data.lhs, data.rhs))
            equate(id, data.lhs);
        equate(id, binary<QueryUnionTag>(data.rhs, data.lhs));
        for (const auto form : forms(data.lhs))
        {
            if (m_report->budget_exhausted)
                return;
            const auto expression = node(form);
            if (query_data_if<QueryEmptyTag>(expression) != nullptr)
                equate(id, data.rhs);
            else if (const auto* inner = query_data_if<QueryUnionTag>(expression))
                equate(id, binary<QueryUnionTag>(inner->lhs, binary<QueryUnionTag>(inner->rhs, data.rhs)));
            else if (const auto* inner = query_data_if<QueryJoinTag>(expression))
            {
                if (same_set(expression.columns(), node(data.rhs).columns()) && (equal(inner->lhs, data.rhs) || equal(inner->rhs, data.rhs)))
                    equate(id, data.rhs);
                for (const auto right_form : forms(data.rhs))
                {
                    const auto other = node(right_form);
                    if (const auto* peer = query_data_if<QueryJoinTag>(other))
                    {
                        const std::array left { inner->lhs, inner->rhs }, right { peer->lhs, peer->rhs };
                        for (size_t l = 0; l < 2; ++l)
                            for (size_t r = 0; r < 2; ++r)
                                if (equal(left[l], right[r]) && same_columns(node(left[1 - l]).columns(), node(right[1 - r]).columns()))
                                    equate(id, binary<QueryJoinTag>(left[l], binary<QueryUnionTag>(left[1 - l], right[1 - r])));
                    }
                }
            }
        }
        for (const auto form : forms(data.rhs))
        {
            const auto expression = node(form);
            if (query_data_if<QueryEmptyTag>(expression) != nullptr)
                equate(id, data.lhs);
            else if (const auto* peer = query_data_if<QueryUnionTag>(expression))
                equate(id, binary<QueryUnionTag>(binary<QueryUnionTag>(data.lhs, peer->lhs), peer->rhs));
        }
    }
    void rewrite(ExpressionId id, QueryView<Values, QueryDifferenceTag> query)
    {
        const auto& data = query.get_data();
        const auto columns = own_columns(node(id).columns());
        if (equal(data.lhs, data.rhs))
            equate(id, empty(columns));
        for (const auto form : forms(data.lhs))
            if (query_data_if<QueryEmptyTag>(node(form)) != nullptr)
                equate(id, empty(columns));
        for (const auto form : forms(data.rhs))
            if (query_data_if<QueryEmptyTag>(node(form)) != nullptr)
                equate(id, data.lhs);
    }
    void rewrite(ExpressionId, QueryView<Values, QueryInputTag>) {}
    void rewrite(ExpressionId, QueryView<Values, QueryEmptyTag>) {}
    void rewrite(ExpressionId, QueryView<Values, QueryDistanceTag>) {}
    void rewrite(ExpressionId, QueryView<Values, QueryGenericJoinTag>) {}

public:
    /// The caller's roots are non-empty and belong to one repository.
    Memo(std::span<const QueryView<Values>> original, const SearchLimits& limits, OptimizationReport& report) :
        m_queries(original.front().get_repository().get_factory().create()),
        m_limits(&limits),
        m_report(&report)
    {
        std::vector<ExpressionId> copies(original.front().get_repository().size());
        for (const auto query : reachable(original))
        {
            const auto source = query.get_index();
            const auto copied = ygg::visit(
                [&]<typename Tag>(QueryView<Values, Tag> child)
                {
                    auto data = checkout<Query<Values, Tag>>(m_builder);
                    *data = child.get_data();
                    detail::query_children<Tag>(*data, [&](ExpressionId& input) { input = copies[input.get_value()]; });
                    return *insert(*data, source, true);
                },
                query.get_variant());
            copies[source.get_value()] = copied;
            m_originals.emplace_back(copied, source);
        }
        for (const auto root : original)
            m_roots.push_back(copies[root.get_index().get_value()]);
        if (size() >= limits.memo_expressions)
            report.budget_exhausted = true;
    }
    /// (memo expression, caller query index) for every query reachable from the caller's roots.
    std::span<const std::pair<ExpressionId, ExpressionId>> originals() const noexcept { return m_originals; }
    std::span<const ExpressionId> roots() const noexcept { return m_roots; }
    QueryView<Values> node(ExpressionId id) const { return QueryView<Values>(id, m_queries); }
    ExpressionAnnotation<Values>& expression(ExpressionId id) { return m_expressions.at(id.get_value()); }
    const ExpressionAnnotation<Values>& expression(ExpressionId id) const { return m_expressions.at(id.get_value()); }
    size_t size() const noexcept { return m_queries.size(); }
    size_t group_count() const noexcept { return m_groups.size(); }
    GroupId group(GroupId id) const
    {
        while (m_groups.at(id.get_value()).parent != id)
            id = m_groups[id.get_value()].parent;
        return id;
    }
    GroupId group(ExpressionId id) const { return group(expression(id).group); }
    ExpressionId representative(GroupId id) const { return m_groups.at(group(id).get_value()).representative; }
    std::vector<ExpressionId> forms(GroupId id) const { return m_groups.at(group(id).get_value()).members; }
    std::vector<ExpressionId> forms(ExpressionId id) const { return forms(group(id)); }
    MemoGroup<Values>& metadata(GroupId id) { return m_groups.at(group(id).get_value()); }
    const MemoGroup<Values>& metadata(GroupId id) const { return m_groups.at(group(id).get_value()); }
    /// Applies every rule to every expression per round, like egglog, until no
    /// expression or merge is added or the round or expression budget is reached.
    void saturate()
    {
        size_t rounds = 0;
        do
        {
            if (rounds++ == m_limits->saturation_rounds)
            {
                m_report->budget_exhausted = true;
                break;
            }
            m_changed = false;
            const auto count = size();
            for (size_t value = 0; value < count && !m_report->budget_exhausted; ++value)
            {
                const auto id = ExpressionId(to_uint_t(value));
                ygg::visit([&]<typename Child>(Child child) { rewrite(id, child); }, node(id).get_variant());
            }
            // Group merges change canonical identities, never stored query edges.
            m_indices.clear();
            for (size_t value = 0; value < size(); ++value)
            {
                const auto id = ExpressionId(to_uint_t(value));
                ygg::visit(
                    [&]<typename Child>(Child child)
                    {
                        const auto& data = child.get_data();
                        auto& bucket = m_indices[key(data)];
                        for (const auto candidate : bucket)
                            if (matches(candidate, data))
                            {
                                merge(id, candidate);
                                return;
                            }
                        bucket.push_back(id);
                    },
                    node(id).get_variant());
            }
        } while (m_changed && !m_report->budget_exhausted);
        m_report->memo_expressions = size();
    }
};

template<ColumnTypes Values>
FactorIdentity factor_identity(MemoGroupId<Values> id)
{
    return MemoFactorIdentity { id.get_value() };
}
template<ColumnTypes Values>
FactorIdentity factor_identity(Index<Query<Values>> id)
{
    return PlanFactorIdentity { id.get_value() };
}

inline Estimate factor(RelationStatistics stats, std::span<const ColumnLayout> columns, FactorIdentity identity)
{
    Estimate result;
    result.rows = stats.rows;
    Factor f { std::move(identity), stats.rows, {} };
    for (size_t i = 0; i < columns.size(); ++i)
    {
        const auto label = columns[i].label;
        const auto found = stats.distinct.find(label);
        const auto ndv = found == stats.distinct.end() ? stats.rows : found->second;
        result.distinct[label] = ndv;
        f.attributes.emplace_back(i, label.get_value(), ndv);
    }
    result.factors.push_back(std::move(f));
    return result;
}

inline Estimate conjunction(Estimate result, std::span<const ColumnLayout> columns)
{
    std::map<uint_t, uint_t> parent;
    for (const auto& f : result.factors)
        for (const auto& [slot, column, ndv] : f.attributes)
            parent[column] = column;
    const auto find = [&](uint_t c)
    {
        while (parent.at(c) != c)
            c = parent.at(c);
        return c;
    };
    for (const auto& [a, b] : result.equalities)
    {
        const auto x = find(a), y = find(b);
        parent[std::max(x, y)] = std::min(x, y);
    }
    std::map<uint_t, std::vector<std::byte>> bound;
    bool zero = false;
    for (const auto& [c, value] : result.constants)
    {
        const auto [it, inserted] = bound.emplace(find(c), value);
        if (!inserted && it->second != value)
            zero = true;
    }
    std::map<uint_t, std::vector<double>> domains;
    std::set<std::pair<FactorIdentity, std::vector<std::pair<size_t, uint_t>>>> unique;
    double logarithm = 0;
    for (const auto& f : result.factors)
    {
        std::vector<std::pair<size_t, uint_t>> bindings;
        for (const auto& [slot, column, ndv] : f.attributes)
            bindings.emplace_back(slot, find(column));
        if (!unique.emplace(f.identity, std::move(bindings)).second)
            continue;
        if (f.rows == 0)
            zero = true;
        else
            logarithm += std::log(f.rows);
        for (const auto& [slot, column, ndv] : f.attributes)
            domains[find(column)].push_back(std::max(1.0, ndv));
    }
    for (const auto& [c, counts] : domains)
    {
        for (const auto n : counts)
            logarithm -= std::log(n);
        if (!bound.contains(c))
            logarithm += std::log(*std::min_element(counts.begin(), counts.end()));
    }
    result.rows = zero ? 0 : std::exp(std::clamp(logarithm, -max_log_estimate, max_log_estimate));
    result.distinct.clear();
    for (const auto& column : columns)
    {
        const auto c = find(column.label.get_value());
        const auto& counts = domains.at(c);
        result.distinct[column.label] = std::min(result.rows, bound.contains(c) ? 1.0 : *std::min_element(counts.begin(), counts.end()));
    }
    return result;
}

inline Estimate combine(Estimate a, const Estimate& b)
{
    for (const auto& factor : b.factors)
        if (std::ranges::none_of(a.factors,
                                 [&](const auto& existing) { return existing.identity == factor.identity && existing.attributes == factor.attributes; }))
            a.factors.push_back(factor);
    a.equalities.insert(b.equalities.begin(), b.equalities.end());
    a.constants.insert(b.constants.begin(), b.constants.end());
    return a;
}

inline void validate_statistics(const RelationStatistics& stats, std::span<const ColumnLayout> columns)
{
    if (!std::isfinite(stats.rows) || stats.rows < 0 || (columns.empty() && stats.rows > 1))
        throw std::invalid_argument("Optimizer: invalid row count.");
    if (stats.work && (!std::isfinite(*stats.work) || *stats.work < 0))
        throw std::invalid_argument("Optimizer: invalid observed work.");
    for (const auto& [label, count] : stats.distinct)
        if (!has(columns, label) || !std::isfinite(count) || count < 0 || count > stats.rows || (stats.rows > 0 && count < 1))
            throw std::invalid_argument("Optimizer: invalid distinct count or column.");
}

/// Missing distinct counts default to min(rows, domain_size), as in verdog.
inline RelationStatistics with_default_distinct(RelationStatistics stats, std::span<const ColumnLayout> columns, double domain_size)
{
    for (const auto& column : columns)
        stats.distinct.try_emplace(column.label, std::min(stats.rows, domain_size));
    return stats;
}

inline Estimate scoped_factor(RelationStatistics stats, std::span<const ColumnLayout> columns, FactorIdentity identity)
{
    for (auto& [column, count] : stats.distinct)
        count = std::min(count, stats.rows);
    return factor(std::move(stats), columns, std::move(identity));
}

template<ColumnTypes Values, std::invocable<Index<Query<Values>>> GetEstimate>
Estimate estimate_operation(QueryView<Values, QueryInputTag> query,
                            std::span<const ColumnLayout> columns,
                            FactorIdentity,
                            GetEstimate&&,
                            const OptimizationContext<Values>& context)
{
    const auto& data = query.get_data();
    const auto domain_size = context.cost.domain_size;
    RelationStatistics stats { columns.empty() ? 1.0 : domain_size, {}, {} };
    if (const auto it = context.statistics.inputs.find(data.input_slot); it != context.statistics.inputs.end())
        stats = it->second;
    return factor(with_default_distinct(std::move(stats), columns, domain_size), columns, InputFactorIdentity { data.input_slot });
}
template<ColumnTypes Values, std::invocable<Index<Query<Values>>> GetEstimate>
Estimate estimate_operation(QueryView<Values, QueryEmptyTag>,
                            std::span<const ColumnLayout> columns,
                            FactorIdentity identity,
                            GetEstimate&&,
                            const OptimizationContext<Values>&)
{
    return factor({ 0, {}, {} }, columns, identity);
}
template<ColumnTypes Values, std::invocable<Index<Query<Values>>> GetEstimate>
Estimate estimate_operation(QueryView<Values, QueryJoinTag> query,
                            std::span<const ColumnLayout> columns,
                            FactorIdentity,
                            GetEstimate&& child,
                            const OptimizationContext<Values>&)
{
    const auto& data = query.get_data();
    return conjunction(combine(child(data.lhs), child(data.rhs)), columns);
}
template<ColumnTypes Values, std::invocable<Index<Query<Values>>> GetEstimate>
Estimate estimate_operation(QueryView<Values, QueryGenericJoinTag> query,
                            std::span<const ColumnLayout> columns,
                            FactorIdentity identity,
                            GetEstimate&& child,
                            const OptimizationContext<Values>&)
{
    const auto& data = query.get_data();
    if (data.inputs.empty())
        return factor({ 1, {}, {} }, columns, identity);
    auto result = child(data.inputs.front());
    for (size_t i = 1; i < data.inputs.size(); ++i)
        result = combine(std::move(result), child(data.inputs[i]));
    return conjunction(std::move(result), columns);
}
template<ColumnTypes Values, std::invocable<Index<Query<Values>>> GetEstimate>
Estimate estimate_operation(QueryView<Values, QuerySelectEqualTag> query,
                            std::span<const ColumnLayout> columns,
                            FactorIdentity,
                            GetEstimate&& child,
                            const OptimizationContext<Values>&)
{
    const auto& data = query.get_data();
    auto result = child(data.arg);
    result.equalities.emplace(std::min(data.lhs_column.get_value(), data.rhs_column.get_value()), std::max(data.lhs_column.get_value(), data.rhs_column.get_value()));
    return conjunction(std::move(result), columns);
}
template<ColumnTypes Values, std::invocable<Index<Query<Values>>> GetEstimate>
Estimate estimate_operation(QueryView<Values, QuerySelectValueTag> query,
                            std::span<const ColumnLayout> columns,
                            FactorIdentity,
                            GetEstimate&& child,
                            const OptimizationContext<Values>&)
{
    const auto& data = query.get_data();
    auto result = child(data.arg);
    result.constants.emplace(data.column.get_value(), std::vector<std::byte>(data.constant.begin(), data.constant.end()));
    return conjunction(std::move(result), columns);
}
template<ColumnTypes Values, std::invocable<Index<Query<Values>>> GetEstimate>
Estimate estimate_operation(QueryView<Values, QueryRenameTag> query,
                            std::span<const ColumnLayout> columns,
                            FactorIdentity,
                            GetEstimate&& child,
                            const OptimizationContext<Values>&)
{
    const auto& data = query.get_data();
    auto result = child(data.arg);
    std::map<uint_t, uint_t> map;
    const auto old = query.get_arg().columns();
    for (size_t i = 0; i < old.size(); ++i)
        map[old[i].label.get_value()] = columns[i].label.get_value();
    for (auto& f : result.factors)
        for (auto& [slot, column, ndv] : f.attributes)
            column = map.at(column);
    decltype(result.equalities) equalities;
    for (const auto& [x, y] : result.equalities)
        equalities.emplace(std::min(map.at(x), map.at(y)), std::max(map.at(x), map.at(y)));
    decltype(result.constants) constants;
    for (const auto& [c, value] : result.constants)
        constants.emplace(map.at(c), value);
    result.equalities = std::move(equalities);
    result.constants = std::move(constants);
    return conjunction(std::move(result), columns);
}
template<ColumnTypes Values, std::invocable<Index<Query<Values>>> GetEstimate>
Estimate estimate_operation(QueryView<Values, QueryProjectTag> query,
                            std::span<const ColumnLayout> columns,
                            FactorIdentity identity,
                            GetEstimate&& child,
                            const OptimizationContext<Values>&)
{
    const auto& data = query.get_data();
    auto a = child(data.arg);
    if (same_set(columns, query.get_arg().columns()))
        return a;
    RelationStatistics result { a.rows, {}, {} };
    double combinations = 1;
    // Equated output columns describe one domain, not independent factors.
    std::map<uint_t, uint_t> parent;
    for (const auto& [column, count] : a.distinct)
        parent[column.get_value()] = column.get_value();
    const auto find = [&](uint_t column)
    {
        while (parent.at(column) != column)
            column = parent.at(column);
        return column;
    };
    for (const auto& [x, y] : a.equalities)
    {
        const auto p = find(x), q = find(y);
        parent[std::max(p, q)] = std::min(p, q);
    }
    std::set<uint_t> seen;
    for (const auto& column : columns)
    {
        result.distinct[column.label] = a.distinct.at(column.label);
        if (seen.insert(find(column.label.get_value())).second)
            combinations = multiply(combinations, a.distinct.at(column.label));
    }
    result.rows = std::min(a.rows, combinations);
    return scoped_factor(std::move(result), columns, identity);
}
template<ColumnTypes Values, std::invocable<Index<Query<Values>>> GetEstimate>
Estimate estimate_operation(QueryView<Values, QueryUnionTag> query,
                            std::span<const ColumnLayout> columns,
                            FactorIdentity identity,
                            GetEstimate&& child,
                            const OptimizationContext<Values>& context)
{
    const auto& data = query.get_data();
    const auto a = child(data.lhs), b = child(data.rhs);
    RelationStatistics result { a.rows, a.distinct, {} };
    double combinations = 1;
    for (auto& [column, count] : result.distinct)
    {
        count = std::min(context.cost.domain_size, bounded(count + b.distinct.at(column)));
        combinations = multiply(combinations, count);
    }
    result.rows = std::min(bounded(a.rows + b.rows), combinations);
    return scoped_factor(std::move(result), columns, identity);
}
template<ColumnTypes Values, std::invocable<Index<Query<Values>>> GetEstimate>
Estimate estimate_operation(QueryView<Values, QueryDifferenceTag> query,
                            std::span<const ColumnLayout> columns,
                            FactorIdentity identity,
                            GetEstimate&& child,
                            const OptimizationContext<Values>&)
{
    const auto& data = query.get_data();
    const auto a = child(data.lhs);
    return scoped_factor({ a.rows, a.distinct, {} }, columns, identity);
}
template<ColumnTypes Values, std::invocable<Index<Query<Values>>> GetEstimate>
Estimate estimate_operation(QueryView<Values, QueryDistanceTag> query,
                            std::span<const ColumnLayout> columns,
                            FactorIdentity identity,
                            GetEstimate&& child,
                            const OptimizationContext<Values>&)
{
    const auto& data = query.get_data();
    const auto sources = child(data.sources), targets = child(data.targets);
    // Unknown reachability: all source-target pairs is a conservative bound.
    RelationStatistics result { multiply(sources.rows, targets.rows), {}, {} };
    for (const auto& column : columns)
        result.distinct[column.label] = result.rows;
    return scoped_factor(std::move(result), columns, identity);
}

/// An observation of the query's source expression replaces the derived estimate.
template<ColumnTypes Values, class Identity, std::invocable<Index<Query<Values>>> GetEstimate>
Estimate
estimate(QueryView<Values> query,
         std::type_identity_t<std::optional<Index<Query<Values>>>> source,
         Identity identity,
         GetEstimate&& child,
         const OptimizationContext<Values>& context)
{
    if (source)
        if (const auto it = context.statistics.expressions.find(*source); it != context.statistics.expressions.end())
            return factor(with_default_distinct(it->second, query.columns(), context.cost.domain_size), query.columns(), factor_identity(identity));
    return ygg::visit([&]<typename Child>(Child operation)
                      { return estimate_operation(operation, query.columns(), factor_identity(identity), child, context); },
                      query.get_variant());
}

/// Freezes one estimate per group before physical extraction (verdog plan_costs):
/// the group's witness with the fewest nodes defines it, ties prefer the earlier
/// witness. Repeated filters therefore cannot shrink an estimate through a cycle.
template<ColumnTypes Values>
void freeze_estimates(Memo<Values>& memo, const OptimizationContext<Values>& context)
{
    using ExpressionId = Index<Query<Values>>;
    constexpr auto unknown = std::numeric_limits<size_t>::max();
    std::vector<size_t> nodes(memo.group_count(), unknown);
    std::vector<ExpressionId> best(memo.group_count());
    for (bool changed = true; changed;)
    {
        changed = false;
        for (size_t value = 0; value < memo.size(); ++value)
        {
            const auto id = ExpressionId(to_uint_t(value));
            size_t count = 1;
            for_each_child(memo.node(id),
                           [&](QueryView<Values> child)
                           {
                               const auto size = nodes[memo.group(child.get_index()).get_value()];
                               count = size == unknown || count == unknown ? unknown : std::min(unknown - 1, count + size);
                           });
            auto& current = nodes[memo.group(id).get_value()];
            if (count < current)
            {
                current = count;
                best[memo.group(id).get_value()] = id;
                changed = true;
            }
        }
    }
    // A best witness has strictly more nodes than its children's best witnesses.
    std::vector<size_t> order;
    for (size_t value = 0; value < memo.group_count(); ++value)
        if (nodes[value] != unknown)
            order.push_back(value);
    std::ranges::stable_sort(order, {}, [&](size_t group) { return nodes[group]; });
    for (const auto value : order)
    {
        const auto group = MemoGroupId<Values>(to_uint_t(value));
        const auto witness = best[value];
        memo.metadata(group).estimate = estimate(memo.node(witness),
                                                 memo.expression(witness).source,
                                                 group,
                                                 [&](ExpressionId child) { return memo.metadata(memo.group(child)).estimate; },
                                                 context);
    }
}

namespace cost_detail
{
template<ColumnTypes Values>
size_t common_columns(QueryView<Values> lhs, QueryView<Values> rhs)
{
    return std::ranges::count_if(lhs.columns(), [&](const auto& column) { return has(rhs.columns(), column.label); });
}
}  // namespace cost_detail

/// verdog's physical cost of one operator; width counts columns and rows come
/// from the frozen estimates. Retained cells are rows * (width + retained_overhead_cells).
template<ColumnTypes Values, std::invocable<QueryView<Values>> GetRows>
OwnCost own_cost(QueryView<Values> plan, double rows, GetRows&& rows_of, const OptimizationContext<Values>& context)
{
    const auto width = static_cast<double>(plan.columns().size());
    const auto cells = std::max(1.0, width);
    const auto stored = multiply(rows, width + retained_overhead_cells);
    return ygg::visit(
        [&]<typename Tag>(QueryView<Values, Tag> operation) -> OwnCost
        {
            if constexpr (std::same_as<Tag, QueryInputTag>)
            {
                const auto it = context.statistics.inputs.find(operation.get_input_slot());
                const bool measured = it != context.statistics.inputs.end() && it->second.work;
                return { measured ? *it->second.work : multiply(rows, cells), stored };
            }
            else if constexpr (std::same_as<Tag, QueryEmptyTag> || std::same_as<Tag, QueryRenameTag>)
                return {};  // Renamed views alias their input storage.
            else if constexpr (std::same_as<Tag, QueryProjectTag>)
                return { width == 0 ? 1.0 : multiply(rows_of(operation.get_arg()), width), stored };
            else if constexpr (FilterTag<Tag>)
                return { bounded(rows_of(operation.get_arg()) + multiply(rows, cells)), stored };
            else if constexpr (std::same_as<Tag, QueryUnionTag>)
                return { multiply(bounded(rows_of(operation.get_lhs()) + rows_of(operation.get_rhs())), cells), stored };
            else if constexpr (std::same_as<Tag, QueryDifferenceTag>)
                return { multiply(bounded(rows_of(operation.get_lhs()) + rows), cells), stored };
            else if constexpr (std::same_as<Tag, QueryJoinTag>)
            {
                const auto lhs = rows_of(operation.get_lhs()), rhs = rows_of(operation.get_rhs());
                const auto keys = static_cast<double>(cost_detail::common_columns(operation.get_lhs(), operation.get_rhs()));
                if (lhs == 0 || rhs == 0)
                    return { 0, stored };
                if (keys == 0)
                    return { multiply(multiply(lhs, rhs), cells), stored };
                return { bounded(multiply(bounded(lhs + rhs), keys) + multiply(rows, keys + cells)), stored };
            }
            else if constexpr (std::same_as<Tag, QueryGenericJoinTag>)
            {
                // ponytail: not in verdog; scans every input and writes the output.
                double inputs = rows;
                for (const auto input : operation.get_inputs())
                    inputs = bounded(inputs + rows_of(QueryView<Values>(input, plan.get_repository())));
                return { multiply(inputs, cells), stored };
            }
            else
            {
                static_assert(std::same_as<Tag, QueryDistanceTag>);
                // ponytail: not in verdog; one graph search per source, then output.
                const auto sources = rows_of(operation.get_sources()), edges = rows_of(operation.get_edges());
                return { bounded(multiply(sources, edges + 1) + rows_of(operation.get_targets()) + rows), stored };
            }
        },
        plan.get_variant());
}

/// Bounded physical extraction (verdog extract_physical): each group keeps a
/// frontier of candidates, compared by the complete cost of their DAGs.
/// ponytail: bounded alternatives, not globally optimal DAG extraction.
template<ColumnTypes Values>
class Search
{
    using PlanId = Index<Query<Values>>;
    using ExpressionId = Index<Query<Values>>;
    using GroupId = MemoGroupId<Values>;
    Memo<Values>* m_memo;
    const OptimizationContext<Values>* m_context;
    OptimizationReport* m_report;
    QueryRepository<Values> m_plans;
    QueryBuilder<Values> m_builder;
    std::vector<Candidate<Values>> m_candidates;

    QueryView<Values> plan(PlanId id) const { return QueryView<Values>(id, m_plans); }
    const Candidate<Values>& candidate(PlanId id) const { return m_candidates.at(id.get_value()); }

    /// Sums the distinct operators of the plans reachable from the roots.
    OwnCost total(std::span<const PlanId> roots) const
    {
        std::vector<bool> seen(m_plans.size());
        OwnCost result;
        std::vector<PlanId> pending(roots.begin(), roots.end());
        while (!pending.empty())
        {
            const auto id = pending.back();
            pending.pop_back();
            if (seen[id.get_value()])
                continue;
            seen[id.get_value()] = true;
            result.work = bounded(result.work + candidate(id).own.work);
            result.retained = bounded(result.retained + candidate(id).own.retained);
            for_each_child(plan(id), [&](QueryView<Values> child) { pending.push_back(child.get_index()); });
        }
        return result;
    }
    double score(const OwnCost& cost) const { return bounded(cost.work + multiply(cost.retained, m_context->cost.memory_weight)); }

    /// Clones a memo expression onto chosen child candidates and prices it once.
    PlanId build(ExpressionId expression, std::span<const QueryView<Values>> children)
    {
        const auto id = detail::clone_query(m_memo->node(expression), children, m_plans, m_builder).get_index();
        if (id.get_value() < m_candidates.size())
            return id;
        assert(id.get_value() == m_candidates.size());
        Candidate<Values> result;
        result.source = m_memo->expression(expression).source;
        result.group = m_memo->group(expression);
        result.rows = m_memo->metadata(result.group).estimate.rows;
        result.own = own_cost(plan(id), result.rows, [&](QueryView<Values> child) { return candidate(child.get_index()).rows; }, *m_context);
        if (result.source)
            if (const auto it = m_context->statistics.expressions.find(*result.source); it != m_context->statistics.expressions.end() && it->second.work)
                result.own.work = *it->second.work;
        result.groups.push_back(result.group);
        for (const auto& child : children)
            result.groups.insert(result.groups.end(), candidate(child.get_index()).groups.begin(), candidate(child.get_index()).groups.end());
        std::ranges::sort(result.groups);
        result.groups.erase(std::unique(result.groups.begin(), result.groups.end()), result.groups.end());
        m_candidates.push_back(std::move(result));
        const PlanId root[] { id };
        m_candidates.back().score = score(total(root));
        return id;
    }

    /// The expression's own witness plan; for the roots this is the original query.
    PlanId baseline(ExpressionId id)
    {
        auto& saved = m_memo->expression(id).baseline;
        if (!saved)
        {
            std::vector<QueryView<Values>> children;
            for_each_child(m_memo->node(id), [&](QueryView<Values> child) { children.push_back(plan(baseline(child.get_index()))); });
            saved = build(id, children);
        }
        return *saved;
    }

    /// Keeps at most frontier_size candidates per group, replacing the worst.
    bool keep(std::vector<PlanId>& frontier, PlanId id)
    {
        if (std::ranges::find(frontier, id) != frontier.end())
            return false;
        if (frontier.size() >= std::max<size_t>(1, m_context->limits.frontier_size))
        {
            m_report->pruned = true;
            const auto worst = std::ranges::max_element(frontier, {}, [&](PlanId other) { return std::pair(candidate(other).score, other); });
            if (std::pair(candidate(id).score, id) >= std::pair(candidate(*worst).score, *worst))
                return false;
            frontier.erase(worst);
        }
        frontier.push_back(id);
        return true;
    }

    /// Combines every expression with its children's frontiers until no frontier changes.
    void extract()
    {
        std::vector<std::set<std::vector<PlanId>>> seen(m_memo->size());
        for (bool changed = true; changed;)
        {
            changed = false;
            for (size_t value = 0; value < m_memo->size(); ++value)
            {
                const auto id = ExpressionId(to_uint_t(value));
                const auto group = m_memo->group(id);
                std::vector<std::vector<PlanId>> choices;
                for_each_child(m_memo->node(id), [&](QueryView<Values> child) { choices.push_back(m_memo->metadata(m_memo->group(child.get_index())).frontier); });
                std::vector<PlanId> selection(choices.size());
                std::vector<QueryView<Values>> children;
                const auto enumerate = [&](auto&& self, size_t position) -> bool
                {
                    if (position < choices.size())
                    {
                        for (const auto choice : choices[position])
                        {
                            selection[position] = choice;
                            if (!self(self, position + 1))
                                return false;
                        }
                        return true;
                    }
                    if (!seen[value].insert(selection).second)
                        return true;
                    if (m_report->candidate_evaluations >= m_context->limits.candidate_evaluations)
                    {
                        m_report->budget_exhausted = true;
                        return false;
                    }
                    ++m_report->candidate_evaluations;
                    // A group must not recur below itself, e.g. through an identity projection.
                    if (std::ranges::any_of(selection, [&](PlanId child) { return std::ranges::binary_search(candidate(child).groups, group); }))
                        return true;
                    children.clear();
                    for (const auto child : selection)
                        children.push_back(plan(child));
                    changed |= keep(m_memo->metadata(group).frontier, build(id, children));
                    return true;
                };
                if (!enumerate(enumerate, 0))
                    return;
            }
        }
    }

public:
    /// The roots are the caller's non-empty roots that the memo was built from.
    Search(Memo<Values>& memo, std::span<const QueryView<Values>> roots, const OptimizationContext<Values>& context, OptimizationReport& report) :
        m_memo(&memo),
        m_context(&context),
        m_report(&report),
        m_plans(roots.front().get_repository().get_factory().create())
    {
        for (size_t value = 0; value < memo.size(); ++value)
            memo.expression(ExpressionId(to_uint_t(value))).baseline.reset();
        for (size_t value = 0; value < memo.group_count(); ++value)
            memo.metadata(GroupId(to_uint_t(value))).frontier.clear();
        freeze_estimates(memo, context);
        // Observations of the original queries override the derived group estimates.
        for (const auto& [id, source] : memo.originals())
            if (const auto it = context.statistics.expressions.find(source); it != m_context->statistics.expressions.end())
            {
                const auto group = memo.group(id);
                memo.metadata(group).estimate =
                    factor(with_default_distinct(it->second, memo.node(id).columns(), m_context->cost.domain_size), memo.node(id).columns(), factor_identity(group));
            }
    }

    QueryPlan<Values> run()
    {
        std::vector<PlanId> incumbent;
        for (const auto root : m_memo->roots())
            incumbent.push_back(baseline(root));
        m_report->baseline_score = score(total(incumbent));
        extract();
        std::vector<PlanId> proposal;
        for (const auto root : m_memo->roots())
        {
            const auto& frontier = m_memo->metadata(m_memo->group(root)).frontier;
            proposal.push_back(frontier.empty() ? baseline(root) :
                                                  *std::ranges::min_element(frontier, {}, [&](PlanId id) { return std::pair(candidate(id).score, id); }));
        }
        if (score(total(proposal)) < m_report->baseline_score)
            incumbent = proposal;
        const auto best = total(incumbent);
        m_report->estimated_work = best.work;
        m_report->retained = best.retained;
        m_report->estimated_score = score(best);
        std::vector<QueryView<Values>> output;
        for (const auto root : incumbent)
            output.push_back(plan(root));
        return compile(std::span<const QueryView<Values>>(output));
    }
};

template<ColumnTypes Values>
void validate(std::span<const QueryView<Values>> roots, const OptimizationContext<Values>& context)
{
    if (!std::isfinite(context.cost.memory_weight) || context.cost.memory_weight < 0)
        throw std::invalid_argument("Optimizer: memory_weight must be finite and nonnegative.");
    if (!std::isfinite(context.cost.domain_size) || context.cost.domain_size <= 0)
        throw std::invalid_argument("Optimizer: domain_size must be finite and positive.");
    for (const auto query : reachable(roots))
    {
        ygg::visit(
            [&]<typename Child>(Child child)
            {
                if constexpr (std::same_as<Child, QueryView<Values, QueryInputTag>>)
                    if (const auto it = context.statistics.inputs.find(child.get_input_slot()); it != context.statistics.inputs.end())
                        validate_statistics(it->second, child.columns());
            },
            query.get_variant());
        if (const auto it = context.statistics.expressions.find(query.get_index()); it != context.statistics.expressions.end())
            validate_statistics(it->second, query.columns());
    }
}

template<ColumnTypes Values>
OptimizationResult<Values> optimize(std::span<const QueryView<Values>> roots, const OptimizationContext<Values>& context)
{
    validate(roots, context);
    OptimizationResult<Values> result;
    if (roots.empty())
        return result;
    Memo<Values> memo(roots, context.limits, result.report);
    memo.saturate();
    Search<Values> search(memo, roots, context, result.report);
    result.plan = search.run();
    return result;
}
}  // namespace ygg::database::optimization_detail
#endif
