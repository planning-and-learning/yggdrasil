/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/datalog/detail/goal_search.hpp"

#include "yggdrasil/database/datalog/detail/executor.hpp"

#include <algorithm>
#include <functional>
#include <ranges>
#include <set>

namespace ygg::database::datalog::detail
{
namespace
{
bool reads_static_input(const CompiledProgram& program, const Literal& literal)
{
    if (literal.kind != Literal::Kind::POSITIVE)
        return false;
    const auto& table = *program.tables[literal.table];
    return table.slot.has_value() && table.is_static;
}

/// The variables of the terms.
template<typename Terms>
bool all_bound(const Terms& terms, const std::set<uint_t>& bound)
{
    return std::ranges::all_of(terms, [&](Term t) { return !t.is_variable() || bound.contains(t.value); });
}

std::vector<Term> project(std::span<const Term> terms, std::span<const size_t> columns)
{
    auto projected = std::vector<Term>();
    for (const auto c : columns)
        projected.push_back(terms[c]);
    return projected;
}

/// A table for reversed rules to read head rows from, and their plans.
void plan_reversed(CompiledProgram& program, size_t arity, std::vector<CompiledRule>& edges)
{
    program.tables.push_back(std::make_unique<Table>(arity));
    program.recursive_tables.push_back(false);
    for (auto& edge : edges)
        plan_rule(program, edge);
}

/// Runs the reversed rules from the head row `row` (followed by weight 0), into `derived`: read rows, then the weight.
void run_reversed(CompiledProgram& program,
                  Executor& executor,
                  size_t scratch,
                  std::span<const CompiledRule> edges,
                  std::vector<uint_t>& row,
                  std::vector<Version>& reads,
                  std::vector<std::vector<uint_t>>& derived)
{
    auto& table = *program.tables[scratch];
    row.push_back(0);
    table.frontier.clear();
    table.frontier.insert(row);
    derived.clear();
    for (const auto& edge : edges)
    {
        reads.assign(edge.literals.size(), Version::CURRENT);
        reads[0] = Version::FRONTIER;
        executor.run(edge, edge.by_source[0], reads, [&](std::span<const uint_t> found) { derived.emplace_back(found.begin(), found.end()); });
    }
    table.frontier.clear();
}
}  // namespace

std::optional<GoalSearch> GoalSearch::make(CompiledProgram& program, const StratumInfo& stratum, std::span<const size_t> goals)
{
    if (goals.empty() || stratum.tables.size() != 1)
        return std::nullopt;
    auto result = GoalSearch { stratum.tables.front(), {}, {}, {}, {} };
    const auto table = result.table;
    const auto key_arity = program.tables[table]->arity - 1;
    for (const auto r : stratum.rules)
    {
        const auto& rule = program.rules[r];
        auto sources = std::vector<size_t>();
        for (size_t j = 0; j < rule.literals.size(); ++j)
            if (rule.literals[j].kind == Literal::Kind::POSITIVE && rule.literals[j].table == table)
                sources.push_back(j);
        if (sources.empty())
            continue;  // A base rule; the targets' weights do not depend on it.
        // Weights are sums along derivations: max (bottleneck weights) would need another combination.
        if (sources.size() != 1
            || std::ranges::any_of(rule.literals,
                                   [](const Literal& l) { return l.kind == Literal::Kind::ARITHMETIC && l.operation != Arithmetic::Operation::ADD; }))
            return std::nullopt;
        result.rules.emplace_back(r, sources.front());
    }
    if (result.rules.empty())
        return std::nullopt;
    // A column that a goal leaves unbound (any value is a target) is no target column.
    auto keys = std::vector<std::vector<Term>>();
    for (size_t c = 0; c < key_arity; ++c)
        result.target_columns.push_back(c);
    for (const auto g : goals)
    {
        const auto& rule = program.rules[g];
        auto bound = std::set<uint_t>();
        for (const auto& literal : rule.literals)
        {
            if (literal.kind == Literal::Kind::POSITIVE && literal.table == table)
                keys.emplace_back(literal.terms.begin(), literal.terms.end() - 1);
            else if (literal.kind == Literal::Kind::POSITIVE || literal.kind == Literal::Kind::ARITHMETIC)
                for (const auto t : literal.terms)
                    if (t.is_variable())
                        bound.insert(t.value);
        }
        std::erase_if(result.target_columns, [&](size_t c) { return keys.back()[c].is_variable() && !bound.contains(keys.back()[c].value); });
        auto columns = std::vector<size_t>();
        for (size_t i = 0; i + 1 < rule.head_terms.size(); ++i)
            columns.push_back(static_cast<size_t>(std::ranges::find(keys.back(), rule.head_terms[i]) - keys.back().begin()));
        result.goal_columns.push_back(std::move(columns));
    }
    if (result.target_columns.empty())
        return std::nullopt;
    for (size_t i = 0; i < goals.size(); ++i)
    {
        const auto& rule = program.rules[goals[i]];
        auto target = rule;
        target.literals.clear();
        for (const auto& literal : rule.literals)
            if (!(literal.kind == Literal::Kind::POSITIVE && literal.table == table))
                target.literals.push_back(literal);
        target.head_terms = project(keys[i], result.target_columns);
        plan_rule(program, target);
        result.targets.push_back(std::move(target));
    }
    return result;
}

std::optional<std::vector<CompiledRule>> GoalSearch::reverse(CompiledProgram& program, std::span<const size_t> columns, size_t scratch, bool abstract) const
{
    auto edges = std::vector<CompiledRule>();
    for (const auto& [r, j] : rules)
    {
        const auto& rule = program.rules[r];
        const auto& source = rule.literals[j];
        const auto zero = [&](Term t) { return t == source.terms.back() ? Term::constant(0) : t; };
        auto edge = rule;
        edge.literals.clear();
        auto head = Literal { Literal::Kind::POSITIVE, scratch, project(rule.head_terms, columns) };
        head.terms.push_back(Term::variable(static_cast<uint_t>(edge.slots++)));
        edge.literals.push_back(head);
        for (size_t i = 0; i < rule.literals.size(); ++i)
        {
            auto literal = rule.literals[i];
            const auto computed = literal.kind == Literal::Kind::ARITHMETIC || literal.kind == Literal::Kind::COMPARISON;
            if (i == j || !(computed || (abstract ? reads_static_input(program, literal) : literal.table != npos)))
                continue;
            if (literal.kind == Literal::Kind::ARITHMETIC)
                for (size_t t = 0; t < 2; ++t)
                    literal.terms[t] = zero(literal.terms[t]);
            edge.literals.push_back(literal);
        }
        edge.head_terms = project(source.terms, columns);
        edge.head_terms.push_back(zero(rule.head_terms.back()));
        // Atoms bind their variables, arithmetic its result once its operands are bound; the other literals are filters.
        auto bound = std::set<uint_t>();
        const auto bind = [&](const auto& terms)
        {
            for (const auto t : terms)
                if (t.is_variable())
                    bound.insert(t.value);
        };
        bind(head.terms);
        for (bool grew = true; grew;)
        {
            const auto before = bound.size();
            for (const auto& literal : edge.literals)
                if (literal.kind == Literal::Kind::POSITIVE || literal.kind == Literal::Kind::AGGREGATE)
                    bind(literal.terms);
                else if (literal.kind == Literal::Kind::ARITHMETIC && all_bound(literal.terms | std::views::take(2), bound))
                    bind(literal.terms | std::views::drop(2));
            grew = bound.size() != before;
        }
        std::erase_if(edge.literals,
                      [&](const Literal& l)
                      {
                          return (l.kind == Literal::Kind::ARITHMETIC || l.kind == Literal::Kind::COMPARISON || l.kind == Literal::Kind::NEGATED)
                                 && !all_bound(l.terms, bound);
                      });
        if (!all_bound(edge.head_terms, bound))
            return std::nullopt;
        edges.push_back(std::move(edge));
    }
    return edges;
}

void GoalSearch::read_targets(Executor& executor, std::vector<Version>& reads, RawArraySet<uint_t>& keys) const
{
    for (const auto& rule : targets)
    {
        reads.assign(rule.literals.size(), Version::CURRENT);
        executor.run(rule, rule.plain, reads, [&](std::span<const uint_t> key) { keys.insert(key); });
    }
}

std::unique_ptr<GoalHeuristic> GoalHeuristic::make(CompiledProgram& program, const GoalSearch& search)
{
    auto result = std::unique_ptr<GoalHeuristic>(new GoalHeuristic(search));
    // The projections: every nonempty set of target columns, or for wide keys the single columns and all of them.
    const auto& targets = search.target_columns;
    auto subsets = std::vector<std::vector<size_t>>();
    if (targets.size() <= 4)
        for (size_t mask = 1; mask < (size_t { 1 } << targets.size()); ++mask)
        {
            auto& columns = subsets.emplace_back();
            for (size_t i = 0; i < targets.size(); ++i)
                if (mask & (size_t { 1 } << i))
                    columns.push_back(targets[i]);
        }
    else
    {
        for (const auto c : targets)
            subsets.push_back({ c });
        subsets.push_back(targets);
    }
    for (auto& columns : subsets)
    {
        const auto scratch = program.tables.size();
        auto edges = search.reverse(program, columns, scratch, true);
        if (!edges)
            continue;
        plan_reversed(program, columns.size() + 1, *edges);
        result->m_projections.push_back(Projection { columns, scratch, std::move(*edges), RawArraySet<uint_t>(columns.size()), {}, nullptr, true });
    }
    if (result->m_projections.empty())
        return nullptr;
    return result;
}

void GoalHeuristic::read_targets(CompiledProgram& program, Executor& executor)
{
    m_target_keys.clear();
    m_search.read_targets(executor, m_reads, m_target_keys);
    const auto& target_columns = m_search.target_columns;
    for (auto& projection : m_projections)
    {
        if (!projection.usable)
            continue;
        auto targets = RawArraySet<uint_t>(projection.columns.size());
        for (uint_t i = 0; i < m_target_keys.size(); ++i)
        {
            m_key.clear();
            for (const auto c : projection.columns)
                m_key.push_back(m_target_keys[i][static_cast<size_t>(std::ranges::find(target_columns, c) - target_columns.begin())]);
            targets.insert(m_key);
        }
        auto rows = std::vector<std::span<const uint_t>>();
        for (uint_t i = 0; i < targets.size(); ++i)
            rows.push_back(targets[i]);
        std::ranges::sort(rows, [](auto lhs, auto rhs) { return std::ranges::lexicographical_compare(lhs, rhs); });
        auto flat = std::vector<uint_t>();
        for (const auto row : rows)
            flat.insert(flat.end(), row.begin(), row.end());
        auto it = projection.weights.find(flat);
        if (it == projection.weights.end())
        {
            auto weights = backward(program, executor, projection, targets);
            if (!weights)
            {
                projection = Projection { {}, projection.scratch, {}, RawArraySet<uint_t>(1), {}, nullptr, false };
                continue;
            }
            it = projection.weights.emplace(std::move(flat), std::move(*weights)).first;
        }
        projection.current = &it->second;
    }
}

uint_t GoalHeuristic::operator()(std::span<const uint_t> key)
{
    auto result = uint_t { 0 };
    for (const auto& projection : m_projections)
    {
        if (!projection.usable)
            continue;
        m_key.clear();
        for (const auto c : projection.columns)
            m_key.push_back(key[c]);
        const auto index = projection.keys.find(m_key);
        if (!index || *index >= projection.current->size())
            return Arithmetic::infinity;
        result = std::max(result, (*projection.current)[*index]);
    }
    return result;
}

std::optional<std::vector<uint_t>>
GoalHeuristic::backward(CompiledProgram& program, Executor& executor, Projection& projection, const RawArraySet<uint_t>& targets)
{
    // Enough for the projections a search can use; a larger abstract space than this is dropped.
    constexpr size_t budget = size_t { 1 } << 18;
    auto best = std::vector<uint_t>();
    auto queue = std::vector<std::pair<uint_t, uint_t>>();
    const auto relax = [&](std::span<const uint_t> key, uint_t weight)
    {
        const auto index = projection.keys.insert(key);
        if (index >= best.size())
            best.resize(projection.keys.size(), Arithmetic::infinity);
        if (weight < best[index])
        {
            best[index] = weight;
            queue.emplace_back(weight, index);
            std::ranges::push_heap(queue, std::greater {});
        }
    };
    for (uint_t i = 0; i < targets.size(); ++i)
        relax(targets[i], 0);
    auto row = std::vector<uint_t>();
    for (size_t expansions = 0; !queue.empty(); ++expansions)
    {
        if (expansions == budget)
            return std::nullopt;
        std::ranges::pop_heap(queue, std::greater {});
        const auto [weight, current] = queue.back();
        queue.pop_back();
        if (weight != best[current])
            continue;
        // The abstract predecessors of the key, with the weights of their edges to it.
        row.assign(projection.keys[current].begin(), projection.keys[current].end());
        run_reversed(program, executor, projection.scratch, projection.edges, row, m_reads, m_derived);
        for (const auto& derived : m_derived)
            relax(std::span<const uint_t>(derived).first(derived.size() - 1),
                  Arithmetic { Arithmetic::Operation::ADD, {}, {}, 0 }.apply(weight, derived.back()));
    }
    return best;
}

size_t GoalHeuristic::memory_usage() const noexcept
{
    size_t result = m_target_keys.memory_usage();
    for (const auto& projection : m_projections)
    {
        result += projection.keys.memory_usage();
        for (const auto& [targets, weights] : projection.weights)
            result += (targets.capacity() + weights.capacity()) * sizeof(uint_t);
    }
    return result;
}

GoalFrontier::GoalFrontier(const GoalSearch& search, std::vector<size_t> goal_columns, size_t scratch, std::vector<CompiledRule> edges) :
    m_search(search),
    m_goal_columns(std::move(goal_columns)),
    m_scratch(scratch),
    m_edges(std::move(edges)),
    m_goal_keys(std::max<size_t>(m_goal_columns.size(), 1)),
    m_rows(search.target_columns.size()),
    m_sources(search.target_columns.size())
{
}

std::unique_ptr<GoalFrontier> GoalFrontier::make(CompiledProgram& program, const GoalSearch& search)
{
    const auto key_arity = program.tables[search.table]->arity - 1;
    if (search.targets.size() != 1 || search.target_columns.size() != key_arity)
        return nullptr;
    const auto scratch = program.tables.size();
    auto edges = search.reverse(program, search.target_columns, scratch, false);
    if (!edges)
        return nullptr;
    plan_reversed(program, key_arity + 1, *edges);
    return std::unique_ptr<GoalFrontier>(new GoalFrontier(search, search.goal_columns.front(), scratch, std::move(*edges)));
}

std::span<const uint_t> GoalFrontier::goal_key(std::span<const uint_t> key)
{
    m_key.clear();
    for (const auto c : m_goal_columns)
        m_key.push_back(key[c]);
    if (m_key.empty())
        m_key.push_back(0);
    return m_key;
}

void GoalFrontier::read_targets(CompiledProgram&, Executor& executor)
{
    m_goal_keys.clear();
    m_sides.clear();
    m_rows.clear();
    m_sources.clear();
    auto targets = RawArraySet<uint_t>(m_search.target_columns.size());
    m_search.read_targets(executor, m_reads, targets);
    for (uint_t i = 0; i < targets.size(); ++i)
    {
        const auto side = m_goal_keys.insert(goal_key(targets[i]));
        if (side == m_sides.size())
            m_sides.emplace_back();
        m_sides[side].rows.push_back(m_rows.insert(targets[i]));
    }
}

void GoalFrontier::add_source(std::span<const uint_t> key)
{
    m_sources.insert(key);
    if (const auto side = m_goal_keys.find(goal_key(key)); side && m_rows.contains(key))
        m_sides[*side].status = Status::REACHABLE;
}

void GoalFrontier::advance(CompiledProgram& program, Executor& executor, std::span<const uint_t> key)
{
    const auto found = m_goal_keys.find(goal_key(key));
    if (!found)
        return;
    auto& side = m_sides[*found];
    if (side.status != Status::SEARCHING)
        return;
    if (side.next == side.rows.size())
    {
        side.status = Status::UNREACHABLE;
        return;
    }
    // The rows of R from which the next row is derivable in one step, in the current state.
    auto row = std::vector<uint_t>(m_rows[side.rows[side.next]].begin(), m_rows[side.rows[side.next]].end());
    ++side.next;
    run_reversed(program, executor, m_scratch, m_edges, row, m_reads, m_derived);
    for (const auto& derived : m_derived)
    {
        const auto predecessor = std::span<const uint_t>(derived).first(derived.size() - 1);
        const auto size = m_rows.size();
        const auto index = m_rows.insert(predecessor);
        if (index != size)
            continue;
        side.rows.push_back(index);
        if (m_sources.contains(predecessor))
            side.status = Status::REACHABLE;
    }
}

bool GoalFrontier::excluded(std::span<const uint_t> key)
{
    const auto side = m_goal_keys.find(goal_key(key));
    return !side || m_sides[*side].status == Status::UNREACHABLE;
}

size_t GoalFrontier::memory_usage() const noexcept
{
    size_t result = m_goal_keys.memory_usage() + m_rows.memory_usage() + m_sources.memory_usage();
    for (const auto& side : m_sides)
        result += side.rows.capacity() * sizeof(uint_t);
    return result;
}
}  // namespace ygg::database::datalog::detail
