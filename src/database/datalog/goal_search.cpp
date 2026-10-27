/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/datalog/detail/goal_search.hpp"

#include "yggdrasil/database/datalog/detail/executor.hpp"
#include "yggdrasil/database/search/a_star.hpp"
#include "yggdrasil/database/search/backward_frontier.hpp"
#include "yggdrasil/database/search/incremental/tree_repair.hpp"
#include "yggdrasil/database/search/search_tree.hpp"

#include <algorithm>
#include <cstring>
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

std::span<const std::byte> as_vertex(std::span<const uint_t> key) { return std::as_bytes(key); }

void from_vertex(std::span<const std::byte> vertex, std::vector<uint_t>& key)
{
    key.resize(vertex.size() / sizeof(uint_t));
    std::memcpy(key.data(), vertex.data(), vertex.size());
}

/// The head weight is the read row's weight plus 1, by one addition that nothing else reads.
bool adds_one(const CompiledRule& rule, const Literal& source)
{
    const auto weight = source.terms.back();
    const auto head = rule.head_terms.back();
    if (!weight.is_variable() || !head.is_variable())
        return false;
    auto uses = size_t { 0 };
    auto adds = false;
    for (const auto& literal : rule.literals)
    {
        uses += static_cast<size_t>(std::ranges::count(literal.terms, weight) + std::ranges::count(literal.terms, head));
        adds = adds
               || (literal.kind == Literal::Kind::ARITHMETIC && literal.operation == Arithmetic::Operation::ADD && literal.terms[2] == head
                   && ((literal.terms[0] == weight && literal.terms[1] == Term::constant(1))
                       || (literal.terms[1] == weight && literal.terms[0] == Term::constant(1))));
    }
    // The read row, and the addition's operand and result.
    return adds && uses == 3;
}

std::vector<Term> project(std::span<const Term> terms, std::span<const size_t> columns)
{
    auto projected = std::vector<Term>();
    for (const auto c : columns)
        projected.push_back(terms[c]);
    return projected;
}

/// Runs the rules from the values `row` of their inputs, into `derived`.
void run_bound(Executor& executor,
               std::span<const BoundRule> rules,
               std::span<const uint_t> row,
               std::vector<Version>& reads,
               std::vector<std::vector<uint_t>>& derived)
{
    derived.clear();
    for (const auto& bound : rules)
    {
        reads.assign(bound.rule.literals.size(), Version::CURRENT);
        executor.run_bound(bound.rule,
                           bound.plan,
                           reads,
                           bound.inputs,
                           row,
                           [&](std::span<const uint_t> found) { derived.emplace_back(found.begin(), found.end()); });
    }
}
}  // namespace

std::optional<GoalSearch> GoalSearch::make(CompiledProgram& program, const StratumInfo& stratum, std::span<const size_t> goals)
{
    if (goals.empty() || stratum.tables.size() != 1)
        return std::nullopt;
    auto result = GoalSearch { stratum.tables.front(), {}, {}, {}, {}, {} };
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
        for (const auto& literal : target.literals)
            if (literal.table != npos && std::ranges::find(result.target_tables, literal.table) == result.target_tables.end())
                result.target_tables.push_back(literal.table);
        plan_rule(program, target);
        result.targets.push_back(std::move(target));
    }
    return result;
}

std::optional<std::vector<BoundRule>> GoalSearch::reverse(CompiledProgram& program, std::span<const size_t> columns, bool abstract) const
{
    auto edges = std::vector<BoundRule>();
    for (const auto& [r, j] : rules)
    {
        const auto& rule = program.rules[r];
        const auto& source = rule.literals[j];
        if (abstract && std::ranges::all_of(columns, [&](size_t c) { return source.terms[c] == rule.head_terms[c]; }))
            continue;
        const auto zero = [&](Term t) { return t == source.terms.back() ? Term::constant(0) : t; };
        auto edge = rule;
        edge.literals.clear();
        const auto inputs = project(rule.head_terms, columns);
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
        bind(inputs);
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
        auto plan = plan_bound(program, edge, inputs);
        edges.push_back(BoundRule { std::move(edge), inputs, std::move(plan) });
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
    for (const auto column : search.target_columns)
    {
        const auto columns = std::vector<size_t> { column };
        auto edges = search.reverse(program, columns, true);
        if (!edges || edges->empty())
            continue;
        result->m_projections.push_back(Projection { columns, std::move(*edges), RawArraySet<uint_t>(columns.size()) });
    }
    if (result->m_projections.empty())
        return nullptr;
    if (search.goal_columns.size() == 1 && !search.goal_columns.front().empty())
    {
        for (const auto c : search.goal_columns.front())
            if (const auto position = std::ranges::find(search.target_columns, c); position != search.target_columns.end())
                result->m_goal_positions.push_back(static_cast<size_t>(position - search.target_columns.begin()));
        if (result->m_goal_positions.size() != search.goal_columns.front().size())
            result->m_goal_positions.clear();
    }
    result->m_goal_keys = RawArraySet<uint_t>(std::max<size_t>(result->m_goal_positions.size(), 1));
    return result;
}

void GoalHeuristic::read_targets(Executor& executor, bool changed)
{
    if (m_read && !changed)
        return;
    m_read = true;
    m_executor = &executor;
    m_target_keys.clear();
    m_search.read_targets(executor, m_reads, m_target_keys);
    const auto& target_columns = m_search.target_columns;
    auto members = std::vector<std::vector<uint_t>>(m_goal_keys.size());
    for (uint_t i = 0; i < m_target_keys.size(); ++i)
    {
        const auto index = *group(m_target_keys[i], true);
        members.resize(m_goal_keys.size());
        members[index].push_back(i);
    }
    members.resize(m_goal_keys.size());
    m_generations.resize(m_goal_keys.size(), 0);
    for (auto& projection : m_projections)
    {
        while (projection.targets.size() < m_goal_keys.size())
            projection.targets.emplace_back(projection.columns.size());
        projection.wanted.resize(m_goal_keys.size());
        projection.current.resize(m_goal_keys.size(), nullptr);
        for (uint_t index = 0; index < m_goal_keys.size(); ++index)
        {
            auto targets = RawArraySet<uint_t>(projection.columns.size());
            for (const auto i : members[index])
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
            if (projection.wanted[index] != flat)
            {
                ++m_generations[index];
                projection.wanted[index] = std::move(flat);
                projection.targets[index] = std::move(targets);
                projection.current[index] = nullptr;
            }
        }
    }
}

std::optional<uint_t> GoalHeuristic::group(std::span<const uint_t> key, bool target)
{
    m_key.assign(1, 0);
    if (!m_goal_positions.empty())
    {
        m_key.clear();
        for (size_t i = 0; i < m_goal_positions.size(); ++i)
            m_key.push_back(key[target ? m_goal_positions[i] : m_search.goal_columns.front()[i]]);
    }
    if (target)
        return m_goal_keys.insert(m_key);
    return m_goal_keys.find(m_key);
}

const std::vector<uint_t>* GoalHeuristic::weights(Projection& projection, uint_t group)
{
    if (projection.current[group] || projection.wanted[group].empty())
        return projection.current[group];
    auto it = projection.weights.find(projection.wanted[group]);
    if (it == projection.weights.end())
        it = projection.weights.emplace(projection.wanted[group], backward(*m_executor, projection, projection.targets[group])).first;
    projection.current[group] = &it->second;
    return projection.current[group];
}

uint_t GoalHeuristic::operator()(std::span<const uint_t> key)
{
    const auto index = group(key, false);
    if (!index || *index >= m_generations.size())
        return Arithmetic::infinity;
    auto result = uint_t { 0 };
    for (auto& projection : m_projections)
    {
        const auto* weights = this->weights(projection, *index);
        m_key.clear();
        for (const auto c : projection.columns)
            m_key.push_back(key[c]);
        const auto at = projection.keys.find(m_key);
        if (!weights || !at || *at >= weights->size())
            return Arithmetic::infinity;
        result = std::max(result, (*weights)[*at]);
    }
    return result;
}

uint64_t GoalHeuristic::generation(std::span<const uint_t> goal_key) const
{
    const auto index = m_goal_positions.empty() ? m_goal_keys.find(std::vector<uint_t> { 0 }) : m_goal_keys.find(goal_key);
    return index && *index < m_generations.size() ? m_generations[*index] : 0;
}

std::vector<uint_t> GoalHeuristic::backward(Executor& executor, Projection& projection, const RawArraySet<uint_t>& targets)
{
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
    while (!queue.empty())
    {
        std::ranges::pop_heap(queue, std::greater {});
        const auto [weight, current] = queue.back();
        queue.pop_back();
        if (weight != best[current])
            continue;
        // The abstract predecessors of the key, with the weights of their edges to it.
        row.assign(projection.keys[current].begin(), projection.keys[current].end());
        run_bound(executor, projection.edges, row, m_reads, m_derived);
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

GoalFrontier::GoalFrontier(const GoalSearch& search, std::vector<size_t> goal_columns, std::vector<BoundRule> edges) :
    m_search(search),
    m_goal_columns(std::move(goal_columns)),
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
    auto edges = search.reverse(program, search.target_columns, false);
    if (!edges)
        return nullptr;
    return std::unique_ptr<GoalFrontier>(new GoalFrontier(search, search.goal_columns.front(), std::move(*edges)));
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

void GoalFrontier::read_targets(Executor& executor)
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

void GoalFrontier::advance(Executor& executor, std::span<const uint_t> key)
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
    run_bound(executor, m_edges, row, m_reads, m_derived);
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
struct UnitSearch::Key
{
    explicit Key(size_t vertex_size) : tree(vertex_size), targets(vertex_size), added(vertex_size), storage(vertex_size) {}

    database::SearchTree tree;
    database::BackwardFrontier backward;
    database::VertexSet targets;
    database::incremental::EdgeList added;
    database::incremental::TreeRepair repair;
    database::AStarStorage storage;
    std::vector<uint_t> ids;
    /// Per delta rule with partial edges, the stored keys by the rule's columns, up to `indexed` of them.
    struct Index
    {
        explicit Index(size_t width) : parts(width) {}
        RawArraySet<uint_t> parts;
        std::vector<std::vector<uint_t>> ids;
        uint_t indexed = 0;
    };
    std::vector<std::optional<Index>> indexes;
    uint64_t generation = 0;
    bool reset = true;
    bool restart = false;

    /// The targets' indices in the tree, stored with their estimates.
    template<typename H>
    std::span<const uint_t> ensure_targets(const H& heuristic)
    {
        ids.clear();
        for (uint_t i = 0; i < targets.size(); ++i)
            ids.push_back(tree.ensure(targets[i], heuristic));
        return ids;
    }
};

/// The edges between keys of R, by the stratum's rules in the current state: from a key, the rules read it (at weight 0)
/// and derive its successors at weight 1; the reversed rules derive its predecessors. Each call collects into a buffer
/// of its own depth, as a search may check an edge while it generates successors.
struct UnitSearch::Oracle
{
    CompiledProgram* program;
    Executor* executor;
    UnitSearch* search;
    /// Per call depth, the neighbors found and the vertex's row.
    std::vector<std::pair<std::vector<uint_t>, std::vector<uint_t>>> buffers;
    size_t depth = 0;
    std::vector<Version> reads;
    std::vector<uint_t> edge;

    template<typename F>
    void neighbors(std::span<const std::byte> vertex, bool forward, const F& on_vertex)
    {
        if (buffers.size() <= depth)
            buffers.resize(depth + 1);
        auto& [found, row] = buffers[depth++];
        found.clear();
        from_vertex(vertex, row);
        const auto arity = row.size();
        if (forward)
            row.push_back(0);
        for (const auto& bound : forward ? search->m_forward : search->m_reversed)
        {
            reads.assign(bound.rule.literals.size(), Version::CURRENT);
            executor->run_bound(bound.rule,
                                bound.plan,
                                reads,
                                bound.inputs,
                                row,
                                [&](std::span<const uint_t> derived) { found.insert(found.end(), derived.begin(), derived.end() - 1); });
        }
        for (size_t offset = 0; offset < found.size(); offset += arity)
            if (!on_vertex(as_vertex(std::span<const uint_t>(found).subspan(offset, arity))))
                break;
        --depth;
    }
    template<typename F>
    void successors(std::span<const std::byte> vertex, const F& on_vertex)
    { neighbors(vertex, true, on_vertex); }
    template<typename F>
    void predecessors(std::span<const std::byte> vertex, const F& on_vertex)
    { neighbors(vertex, false, on_vertex); }
    bool edge_exists(std::span<const std::byte> from, std::span<const std::byte> to)
    {
        // The read row's key at weight 0, then the head's key.
        from_vertex(from, edge);
        edge.push_back(0);
        const auto offset = edge.size();
        edge.resize(offset + to.size() / sizeof(uint_t));
        std::memcpy(std::span(edge).subspan(offset).data(), to.data(), to.size());
        auto exists = false;
        for (const auto& bound : search->m_edges)
        {
            if (exists)
                break;
            reads.assign(bound.rule.literals.size(), Version::CURRENT);
            executor->run_bound(bound.rule, bound.plan, reads, bound.inputs, edge, [&](std::span<const uint_t>) { exists = true; });
        }
        return exists;
    }
};

namespace
{
/// The GoalHeuristic as a DistanceHeuristic over the bytes of keys of R.
struct Estimate
{
    GoalHeuristic* heuristic;
    std::vector<uint_t>* key;

    [[nodiscard]] uint_t estimate(std::span<const std::byte> vertex) const
    {
        if (!heuristic)
            return 0;
        from_vertex(vertex, *key);
        return (*heuristic)(*key);
    }
};

/// Rows of bytes as a VertexRange.
struct Vertices
{
    const std::vector<std::vector<std::byte>>* rows;

    [[nodiscard]] size_t size() const noexcept { return rows->size(); }
    [[nodiscard]] std::span<const std::byte> row(size_t i) const { return (*rows)[i]; }
};
}  // namespace

UnitSearch::UnitSearch(const GoalSearch& search, std::vector<BoundRule> forward, std::vector<BoundRule> edges, std::vector<BoundRule> reversed) :
    m_search(search),
    m_goal_columns(search.goal_columns.front()),
    m_forward(std::move(forward)),
    m_edges(std::move(edges)),
    m_reversed(std::move(reversed)),
    m_goal_keys(std::max<size_t>(m_goal_columns.size(), 1))
{
}

UnitSearch::~UnitSearch() = default;

std::unique_ptr<UnitSearch> UnitSearch::make(CompiledProgram& program, const StratumInfo& stratum, const GoalSearch& search)
{
    const auto key_arity = program.tables[search.table]->arity - 1;
    if (search.targets.size() != 1 || search.target_columns.size() != key_arity)
        return nullptr;
    for (const auto r : stratum.rules)
    {
        const auto& rule = program.rules[r];
        const auto recursive = std::ranges::find(search.rules, r, &std::pair<size_t, size_t>::first);
        if (recursive == search.rules.end() ? rule.head_terms.back() != Term::constant(0) : !adds_one(rule, rule.literals[recursive->second]))
            return nullptr;
    }
    auto reversed = search.reverse(program, search.target_columns, false);
    if (!reversed)
        return nullptr;
    auto forward = std::vector<BoundRule>(), edges = std::vector<BoundRule>();
    for (const auto& [r, j] : search.rules)
    {
        auto edge = program.rules[r];
        auto inputs = edge.literals[j].terms;
        edge.literals.erase(edge.literals.begin() + static_cast<std::ptrdiff_t>(j));
        auto plan = plan_bound(program, edge, inputs);
        forward.push_back(BoundRule { edge, inputs, std::move(plan) });
        inputs.insert(inputs.end(), edge.head_terms.begin(), edge.head_terms.end() - 1);
        plan = plan_bound(program, edge, inputs);
        edges.push_back(BoundRule { std::move(edge), std::move(inputs), std::move(plan) });
    }
    auto result = std::unique_ptr<UnitSearch>(new UnitSearch(search, std::move(forward), std::move(edges), std::move(*reversed)));
    // Per recursive rule and literal over another table, the rule without its read of R, that literal reading its
    // change: the edges (the read row's key, the head's key) that the change adds.
    for (const auto& [r, j] : search.rules)
    {
        const auto& rule = program.rules[r];
        const auto& source = rule.literals[j];
        for (size_t i = 0; i < rule.literals.size(); ++i)
        {
            const auto& changed = rule.literals[i];
            if (i == j || changed.table == npos || reads_static_input(program, changed))
                continue;
            auto delta = Delta { rule, i - (i > j ? 1 : 0), changed.kind == Literal::Kind::NEGATED ? Version::REMOVED : Version::ADDED, {} };
            delta.rule.literals.erase(delta.rule.literals.begin() + static_cast<std::ptrdiff_t>(j));
            delta.rule.literals[delta.literal].kind = changed.kind == Literal::Kind::AGGREGATE ? Literal::Kind::AGGREGATE : Literal::Kind::POSITIVE;
            auto bound = std::set<uint_t>();
            for (bool grew = true; grew;)
            {
                const auto before = bound.size();
                for (const auto& literal : delta.rule.literals)
                    if (literal.kind == Literal::Kind::POSITIVE || literal.kind == Literal::Kind::AGGREGATE)
                    {
                        for (const auto t : literal.terms)
                            if (t.is_variable())
                                bound.insert(t.value);
                    }
                    else if (literal.kind == Literal::Kind::ARITHMETIC && all_bound(literal.terms | std::views::take(2), bound))
                        bound.insert(literal.terms[2].value);
                grew = bound.size() != before;
            }
            // Filters over what the rule leaves unbound (the read row's weight) are dropped: more edges, which repair
            // only lowers bounds for.
            std::erase_if(delta.rule.literals,
                          [&](const Literal& l)
                          {
                              return (l.kind == Literal::Kind::ARITHMETIC || l.kind == Literal::Kind::COMPARISON || l.kind == Literal::Kind::NEGATED)
                                     && !all_bound(l.terms, bound);
                          });
            delta.literal = static_cast<size_t>(
                std::ranges::find_if(delta.rule.literals, [&](const Literal& l) { return l.table == changed.table && l.terms == changed.terms; })
                - delta.rule.literals.begin());
            // A key column that the rule keeps (the same variable in the read row and the head) and leaves unbound
            // holds any value: the edge exists from every stored key that matches the others.
            const auto is_bound = [&](Term t) { return !t.is_variable() || bound.contains(t.value); };
            for (size_t c = 0; c + 1 < source.terms.size(); ++c)
                if (is_bound(source.terms[c]) || source.terms[c] != rule.head_terms[c])
                    delta.columns.push_back(c);
            delta.rule.head_terms = project(source.terms, delta.columns);
            const auto head = project(rule.head_terms, delta.columns);
            delta.rule.head_terms.insert(delta.rule.head_terms.end(), head.begin(), head.end());
            if (!all_bound(delta.rule.head_terms, bound))
            {
                result->m_opaque.push_back(changed.table);
                continue;
            }
            plan_rule(program, delta.rule);
            result->m_deltas.push_back(std::move(delta));
        }
    }
    return result;
}

std::span<const uint_t> UnitSearch::goal_key(std::span<const uint_t> key)
{
    m_key.clear();
    for (const auto c : m_goal_columns)
        m_key.push_back(key[c]);
    if (m_key.empty())
        m_key.push_back(0);
    return m_key;
}

uint_t UnitSearch::key_search(std::span<const uint_t> goal_key, size_t vertex_size)
{
    const auto index = m_goal_keys.insert(goal_key);
    if (index == m_keys.size())
        m_keys.push_back(std::make_unique<Key>(vertex_size));
    return index;
}

void UnitSearch::reset() noexcept
{
    m_read = false;
    for (auto& key : m_keys)
        key->reset = true;
}

void UnitSearch::changed(CompiledProgram& program, Executor& executor)
{
    ++m_version;
    if (std::ranges::any_of(m_opaque, [&](size_t table) { return program.tables[table]->changed(); }))
    {
        reset();
        return;
    }
    const auto arity = m_search.target_columns.size();
    for (const auto& delta : m_deltas)
    {
        const auto& table = *program.tables[delta.rule.literals[delta.literal].table];
        if (table.rows(delta.change).empty())
            continue;
        m_reads.assign(delta.rule.literals.size(), Version::CURRENT);
        m_reads[delta.literal] = delta.change;
        m_derived.clear();
        executor.run(delta.rule,
                     delta.rule.by_source[delta.literal],
                     m_reads,
                     [&](std::span<const uint_t> edge) { m_derived.emplace_back(edge.begin(), edge.end()); });
        const auto record = [&](Key& key, std::span<const uint_t> from, std::span<const uint_t> to)
        {
            if (key.reset)
                return;
            const auto stored = key.tree.find(as_vertex(from));
            if (!key.restart)
            {
                const auto to_bound = key.backward.bound(key.tree.find(as_vertex(to)));
                key.restart = to_bound != database::infinity && key.backward.bound(stored) > to_bound + 1;
            }
            if (stored)
                key.added.add(as_vertex(from), as_vertex(to));
        };
        const auto width = delta.columns.size();
        auto from = std::vector<uint_t>(arity), to = std::vector<uint_t>(arity);
        if (width == arity)
        {
            for (const auto& edge : m_derived)
            {
                for (size_t i = 0; i < arity; ++i)
                {
                    from[delta.columns[i]] = edge[i];
                    to[delta.columns[i]] = edge[arity + i];
                }
                if (const auto index = m_goal_keys.find(goal_key(from)))
                    record(*m_keys[*index], from, to);
            }
            continue;
        }
        // Partial edges: the stored keys that match a partial edge's read row in `columns` (by an index grown with
        // the tree) take its head there, the other columns stay. An edge from a key that is not stored breaks the
        // backward frontier's bounds if it ends at a found key more than one level inside the unfound ones.
        if (m_derived.empty())
            continue;
        const auto position = static_cast<size_t>(&delta - m_deltas.data());
        auto part = std::vector<uint_t>(width);
        for (auto& key : m_keys)
        {
            if (key->reset)
                continue;
            if (key->indexes.size() <= position)
                key->indexes.resize(m_deltas.size());
            auto& index = key->indexes[position];
            if (!index)
                index.emplace(width);
            for (; index->indexed < key->tree.size(); ++index->indexed)
            {
                from_vertex(key->tree.vertex(index->indexed), from);
                for (size_t i = 0; i < width; ++i)
                    part[i] = from[delta.columns[i]];
                const auto slot = index->parts.insert(part);
                if (slot == index->ids.size())
                    index->ids.emplace_back();
                index->ids[slot].push_back(index->indexed);
            }
            const auto unfound = key->backward.bound(std::nullopt);
            for (const auto& edge : m_derived)
            {
                if (!key->restart)
                    if (const auto heads = index->parts.find(std::span<const uint_t>(edge).subspan(width)))
                        key->restart = std::ranges::any_of(index->ids[*heads],
                                                           [&](uint_t id)
                                                           {
                                                               const auto level = key->backward.bound(id);
                                                               return level != database::infinity && (unfound == database::infinity || level + 1 < unfound);
                                                           });
                const auto slot = index->parts.find(std::span<const uint_t>(edge).first(width));
                if (!slot)
                    continue;
                for (const auto id : index->ids[*slot])
                {
                    from_vertex(key->tree.vertex(id), from);
                    to = from;
                    for (size_t i = 0; i < width; ++i)
                        to[delta.columns[i]] = edge[width + i];
                    record(*key, from, to);
                }
            }
        }
    }
}

void UnitSearch::run(CompiledProgram& program, Executor& executor, std::span<const std::vector<uint_t>> sources, GoalHeuristic* heuristic, bool targets_changed)
{
    const auto arity = m_search.target_columns.size();
    const auto vertex_size = arity * sizeof(uint_t);
    const auto bytes = [](std::span<const uint_t> key)
    {
        const auto vertex = as_vertex(key);
        return std::vector<std::byte>(vertex.begin(), vertex.end());
    };
    // Per goal key, its targets (read anew only if they can have changed) and sources as bytes.
    const auto reread = targets_changed || !m_read;
    if (reread)
    {
        auto targets = RawArraySet<uint_t>(arity);
        m_search.read_targets(executor, m_reads, targets);
        m_key_targets.assign(m_keys.size(), {});
        for (uint_t i = 0; i < targets.size(); ++i)
        {
            const auto index = key_search(goal_key(targets[i]), vertex_size);
            m_key_targets.resize(m_keys.size());
            m_key_targets[index].push_back(bytes(targets[i]));
        }
        m_key_targets.resize(m_keys.size());
        for (size_t index = 0; index < m_keys.size(); ++index)
        {
            auto& key = *m_keys[index];
            const auto& wanted = m_key_targets[index];
            if (key.targets.size() != wanted.size() || !std::ranges::all_of(wanted, [&](const auto& target) { return key.targets.contains(target); }))
            {
                key.targets.clear();
                for (const auto& target : wanted)
                    key.targets.insert(target);
                key.reset = true;
            }
        }
        m_read = true;
    }
    const auto& key_targets = m_key_targets;
    auto key_sources = std::vector<std::vector<std::vector<std::byte>>>();
    key_sources.resize(m_keys.size());
    for (const auto& source : sources)
        if (const auto index = m_goal_keys.find(goal_key(std::span<const uint_t>(source).first(arity))); index && !key_targets[*index].empty())
            key_sources[*index].push_back(bytes(std::span<const uint_t>(source).first(arity)));
    auto oracle = Oracle { &program, &executor, this, {}, 0 };
    auto estimate_key = std::vector<uint_t>();
    const auto estimate = Estimate { heuristic, &estimate_key };
    auto& table = *program.tables[m_search.table];
    for (size_t index = 0; index < m_keys.size(); ++index)
    {
        if (key_sources[index].empty())
            continue;
        auto& key = *m_keys[index];
        if (const auto generation = heuristic ? heuristic->generation(m_goal_keys[static_cast<uint_t>(index)]) : 0; key.generation != generation)
        {
            key.generation = generation;
            key.reset = true;
        }
        if (key.reset)
        {
            key.tree.clear();
            key.indexes.clear();
            key.reset = false;
            key.backward.start(key.tree, key.ensure_targets(estimate));
        }
        else
        {
            if (!key.added.empty())
                key.repair.run(key.tree, key.added, oracle, estimate);
            if (key.restart)
                key.backward.start(key.tree, key.ensure_targets(estimate));
        }
        key.added.clear();
        key.restart = false;
        auto search = database::AStar<Oracle, Estimate>(oracle, key.tree, key.backward, estimate, key.targets, key.storage, m_version, m_search.any_path);
        const auto distance = search.run(Vertices { &key_sources[index] });
        if (distance == database::infinity)
            continue;
        auto reached = std::vector<uint_t>();
        search.path(
            [&](std::span<const std::byte> vertex)
            {
                if (key.targets.contains(vertex))
                    from_vertex(vertex, reached);
            });
        reached.push_back(distance);
        table.current.insert(reached);
    }
}

size_t UnitSearch::memory_usage() const noexcept
{
    size_t result = m_goal_keys.memory_usage();
    for (const auto& key : m_keys)
        result += key->targets.memory_usage() + key->tree.size() * (key->targets.array_size() + 4 * sizeof(uint64_t));
    return result;
}
}  // namespace ygg::database::datalog::detail
