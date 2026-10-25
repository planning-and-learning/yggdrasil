/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yggdrasil/database/datalog/construction.hpp"
#include "yggdrasil/database/datalog/evaluator.hpp"

#include <algorithm>
#include <benchmark/benchmark.h>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <utility>
#include <vector>

/// Per-state cost of Datalog features: full evaluation of a state, and incremental maintenance between two successor
/// states that differ by a small change (forward and backward alternately). Each workload checks once that incremental
/// and full evaluation agree.
namespace ygg::profiling
{
namespace
{
namespace dl = database::datalog;
using Relation = Builder<database::Relation>;
using Tuple = std::vector<uint_t>;
using Tuples = std::set<Tuple>;
using Label = Index<database::Column>;
using Function = dl::AggregateFunction;

dl::Term var(uint_t variable) { return dl::Term::variable(variable); }

Relation relation_of(const Tuples& tuples, size_t arity)
{
    std::vector<Label> labels;
    labels.reserve(arity);
    for (size_t i = 0; i < arity; ++i)
        labels.emplace_back(to_uint_t(i));
    Relation result { std::span<const Label>(labels) };
    std::vector<std::byte> bytes(arity * ByteCodec<uint_t>::size);
    for (const auto& tuple : tuples)
    {
        for (size_t i = 0; i < arity; ++i)
            ByteCodec<uint_t>::encode(tuple[i], std::span(bytes).subspan(i * ByteCodec<uint_t>::size, ByteCodec<uint_t>::size));
        result.insert(std::span<const std::byte>(bytes));
    }
    return result;
}

Tuples minus(const Tuples& lhs, const Tuples& rhs)
{
    Tuples result;
    std::ranges::set_difference(lhs, rhs, std::inserter(result, result.end()));
    return result;
}

/// A program, the inputs of two successor states, and the relations features read.
struct Workload
{
    dl::DatalogRepository repository = dl::DatalogRepositoryFactory {}.create();
    dl::DatalogBuilder builder;
    std::vector<size_t> arities;
    std::vector<Tuples> initial;
    std::vector<Tuples> changed;
    std::vector<dl::RelationView> outputs;
    std::optional<dl::ProgramView> program;
    std::vector<dl::RuleView> rules;

    dl::RelationView input(size_t arity)
    {
        auto data = dl::checkout<dl::Relation>(builder);
        data->id = 1000 + arities.size();
        data->arity = arity;
        data->input_slot = arities.size();
        arities.push_back(arity);
        initial.emplace_back();
        changed.emplace_back();
        return dl::insert(repository, *data).first;
    }
    dl::RelationView derived(size_t id, size_t arity)
    {
        auto data = dl::checkout<dl::Relation>(builder);
        data->id = id;
        data->arity = arity;
        return dl::insert(repository, *data).first;
    }
    dl::AtomView atom(dl::RelationView relation, std::initializer_list<dl::Term> terms)
    {
        auto data = dl::checkout<dl::Atom>(builder);
        data->relation = relation.get_index();
        for (const auto term : terms)
            data->terms.push_back(term);
        return dl::insert(repository, *data).first;
    }
    dl::AggregateView aggregate(Function function, uint_t result, uint_t value, std::initializer_list<dl::AtomView> atoms)
    {
        auto data = dl::checkout<dl::Aggregate>(builder);
        data->function = function;
        data->result = result;
        data->value = value;
        for (const auto atom : atoms)
            data->atoms.push_back(atom.get_index());
        return dl::insert(repository, *data).first;
    }
    void rule(dl::AtomView head,
              std::initializer_list<dl::AtomView> atoms,
              std::initializer_list<dl::AtomView> negated = {},
              std::initializer_list<dl::AggregateView> aggregates = {})
    {
        auto data = dl::checkout<dl::Rule>(builder);
        data->head = head.get_index();
        for (const auto atom : atoms)
            data->atoms.push_back(atom.get_index());
        for (const auto atom : negated)
            data->negated.push_back(atom.get_index());
        for (const auto aggregate : aggregates)
            data->aggregates.push_back(aggregate.get_index());
        rules.push_back(dl::insert(repository, *data).first);
    }
    void finish()
    {
        auto data = dl::checkout<dl::Program>(builder);
        for (const auto rule : rules)
            data->rules.push_back(rule.get_index());
        program = dl::insert(repository, *data).first;
    }
    size_t rows(const std::vector<Tuples>& state) const
    {
        size_t result = 0;
        for (const auto& tuples : state)
            result += tuples.size();
        return result;
    }
};

/// Reachability from a start cell on an n × n grid around blocks; the successor moves one block to a free neighbor.
std::unique_ptr<Workload> grid(uint_t n)
{
    auto w = std::make_unique<Workload>();
    const auto adjacent = w->input(2), start = w->input(1), blocked = w->input(1);
    const auto reach = w->derived(0, 1);
    constexpr uint_t c = 0, d = 1;
    w->rule(w->atom(reach, { var(c) }), { w->atom(start, { var(c) }) });
    w->rule(w->atom(reach, { var(d) }), { w->atom(reach, { var(c) }), w->atom(adjacent, { var(c), var(d) }) }, { w->atom(blocked, { var(d) }) });
    w->outputs = { reach };
    w->finish();
    const auto cell = [&](uint_t row, uint_t column) { return row * n + column; };
    for (uint_t row = 0; row < n; ++row)
        for (uint_t column = 0; column < n; ++column)
        {
            if (row + 1 < n)
            {
                w->initial[0].insert({ cell(row, column), cell(row + 1, column) });
                w->initial[0].insert({ cell(row + 1, column), cell(row, column) });
            }
            if (column + 1 < n)
            {
                w->initial[0].insert({ cell(row, column), cell(row, column + 1) });
                w->initial[0].insert({ cell(row, column + 1), cell(row, column) });
            }
        }
    w->initial[1].insert({ cell(0, 0) });
    std::mt19937 random(3);
    while (w->initial[2].size() < n * n / 5)
        if (const auto block = static_cast<uint_t>(random() % (size_t { n } * n)); block != cell(0, 0))
            w->initial[2].insert({ block });
    w->changed = w->initial;
    // Move a block near the start: it changes what is reachable.
    const auto moved = *w->initial[2].begin();
    w->changed[2].erase(moved);
    for (uint_t candidate = moved[0] + 1; candidate < n * n; ++candidate)
        if (!w->initial[2].contains({ candidate }))
        {
            w->changed[2].insert({ candidate });
            break;
        }
    return w;
}

/// Termes-like: towers of goal heights on an n × n grid; a block at (cell, height) is needed if a needed neighbor one
/// level up stands on it, and not yet built. The successor builds or removes one block. Also counts needed blocks.
std::unique_ptr<Workload> termes(uint_t n, uint_t heights)
{
    auto w = std::make_unique<Workload>();
    const auto goal = w->input(2), support = w->input(2), below = w->input(2), built = w->input(2), cells = w->input(1);
    const auto need = w->derived(0, 2), count = w->derived(1, 2);
    constexpr uint_t c = 0, h = 1, m = 2, l = 3, k = 4;
    w->rule(w->atom(need, { var(c), var(h) }), { w->atom(goal, { var(c), var(h) }) });
    w->rule(w->atom(need, { var(m), var(l) }),
            { w->atom(need, { var(c), var(h) }), w->atom(support, { var(m), var(c) }), w->atom(below, { var(l), var(h) }) },
            { w->atom(built, { var(m), var(l) }) });
    w->rule(w->atom(count, { var(c), var(k) }),
            { w->atom(cells, { var(c) }) },
            {},
            { w->aggregate(Function::COUNT, k, 0, { w->atom(need, { var(c), var(h) }) }) });
    w->outputs = { need, count };
    w->finish();
    // Cells are 0 .. n²-1, heights n² .. n²+heights-1.
    const auto height = [&](uint_t level) { return n * n + level; };
    for (uint_t cell = 0; cell < n * n; ++cell)
    {
        w->initial[4].insert({ cell });
        const auto row = cell / n, column = cell % n;
        for (const auto& [dr, dc] : std::array<std::pair<int, int>, 4> { { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } } })
        {
            const auto nr = static_cast<int>(row) + dr, nc = static_cast<int>(column) + dc;
            if (nr >= 0 && nc >= 0 && nr < static_cast<int>(n) && nc < static_cast<int>(n))
                w->initial[1].insert({ static_cast<uint_t>(nr) * n + static_cast<uint_t>(nc), cell });
        }
    }
    for (uint_t level = 1; level < heights; ++level)
        w->initial[2].insert({ height(level - 1), height(level) });
    std::mt19937 random(5);
    for (uint_t tower = 0; tower < n / 2 + 1; ++tower)
        w->initial[0].insert({ static_cast<uint_t>(random() % (size_t { n } * n)), height(heights - 1) });
    for (size_t i = 0; i < n * n / 4; ++i)
        w->initial[3].insert({ static_cast<uint_t>(random() % (size_t { n } * n)), height(static_cast<uint_t>(random() % heights)) });
    w->changed = w->initial;
    w->changed[3].insert({ (*w->initial[0].begin())[0], height(heights - 2) });
    return w;
}

/// Transitive closure of a random graph with n vertices and 2n edges; the successor replaces one edge.
std::unique_ptr<Workload> closure(uint_t n)
{
    auto w = std::make_unique<Workload>();
    const auto edges = w->input(2);
    const auto tc = w->derived(0, 2);
    constexpr uint_t x = 0, y = 1, z = 2;
    w->rule(w->atom(tc, { var(x), var(y) }), { w->atom(edges, { var(x), var(y) }) });
    w->rule(w->atom(tc, { var(x), var(z) }), { w->atom(tc, { var(x), var(y) }), w->atom(edges, { var(y), var(z) }) });
    w->outputs = { tc };
    w->finish();
    std::mt19937 random(7);
    while (w->initial[0].size() < 2 * size_t { n })
        w->initial[0].insert({ static_cast<uint_t>(random() % n), static_cast<uint_t>(random() % n) });
    w->changed = w->initial;
    w->changed[0].erase(w->changed[0].begin());
    while (w->changed[0].size() < w->initial[0].size())
        w->changed[0].insert({ static_cast<uint_t>(random() % n), static_cast<uint_t>(random() % n) });
    return w;
}

/// Non-recursive: paths of length three in a random graph, and the out-degree of every vertex.
std::unique_ptr<Workload> paths(uint_t n)
{
    auto w = std::make_unique<Workload>();
    const auto edges = w->input(2), vertices = w->input(1);
    const auto path = w->derived(0, 2), degree = w->derived(1, 2);
    constexpr uint_t x = 0, y = 1, z = 2, u = 3, k = 4;
    w->rule(w->atom(path, { var(x), var(u) }), { w->atom(edges, { var(x), var(y) }), w->atom(edges, { var(y), var(z) }), w->atom(edges, { var(z), var(u) }) });
    w->rule(w->atom(degree, { var(x), var(k) }),
            { w->atom(vertices, { var(x) }) },
            {},
            { w->aggregate(Function::COUNT, k, 0, { w->atom(edges, { var(x), var(y) }) }) });
    w->outputs = { path, degree };
    w->finish();
    std::mt19937 random(11);
    for (uint_t vertex = 0; vertex < n; ++vertex)
        w->initial[1].insert({ vertex });
    while (w->initial[0].size() < 3 * size_t { n })
        w->initial[0].insert({ static_cast<uint_t>(random() % n), static_cast<uint_t>(random() % n) });
    w->changed = w->initial;
    w->changed[0].erase(w->changed[0].begin());
    w->changed[0].insert({ static_cast<uint_t>(random() % n), static_cast<uint_t>(random() % n) });
    return w;
}

/// A cyclic body: triangles in a skewed graph, a hub with edges to and from every vertex plus 2n random edges, so that
/// the hub has about n² wedges but few triangles.
std::unique_ptr<Workload> triangles(uint_t n)
{
    auto w = std::make_unique<Workload>();
    const auto edges = w->input(2);
    const auto triangle = w->derived(0, 3);
    constexpr uint_t x = 0, y = 1, z = 2;
    w->rule(w->atom(triangle, { var(x), var(y), var(z) }),
            { w->atom(edges, { var(x), var(y) }), w->atom(edges, { var(y), var(z) }), w->atom(edges, { var(z), var(x) }) });
    w->outputs = { triangle };
    w->finish();
    std::mt19937 random(13);
    for (uint_t vertex = 1; vertex < n; ++vertex)
    {
        w->initial[0].insert({ 0, vertex });
        w->initial[0].insert({ vertex, 0 });
    }
    while (w->initial[0].size() < 4 * size_t { n })
        w->initial[0].insert({ static_cast<uint_t>(1 + random() % (n - 1)), static_cast<uint_t>(1 + random() % (n - 1)) });
    w->changed = w->initial;
    w->changed[0].erase(std::prev(w->changed[0].end()));
    while (w->changed[0].size() < w->initial[0].size())
        w->changed[0].insert({ static_cast<uint_t>(1 + random() % (n - 1)), static_cast<uint_t>(1 + random() % (n - 1)) });
    return w;
}

/// Both states' input relations and changes.
struct Prepared
{
    std::unique_ptr<Workload> workload;
    std::vector<Relation> initial, changed, forward_added, forward_removed;
    /// The input slots that both states share, read once by the evaluator.
    std::vector<size_t> static_slots;

    explicit Prepared(std::unique_ptr<Workload> w) : workload(std::move(w))
    {
        for (size_t slot = 0; slot < workload->arities.size(); ++slot)
            if (workload->initial[slot] == workload->changed[slot])
                static_slots.push_back(slot);
        for (size_t slot = 0; slot < workload->arities.size(); ++slot)
        {
            const auto arity = workload->arities[slot];
            initial.push_back(relation_of(workload->initial[slot], arity));
            changed.push_back(relation_of(workload->changed[slot], arity));
            forward_added.push_back(relation_of(minus(workload->changed[slot], workload->initial[slot]), arity));
            forward_removed.push_back(relation_of(minus(workload->initial[slot], workload->changed[slot]), arity));
        }
        check();
    }
    [[nodiscard]] dl::Evaluator evaluator() const
    {
        YGG_ASSUME(workload->program.has_value());
        return dl::Evaluator(*workload->program, workload->outputs, static_slots);
    }

    [[nodiscard]] auto changes(bool forward) const
    {
        using Change = std::pair<const Relation&, const Relation&>;
        std::vector<Change> result;
        result.reserve(initial.size());
        for (size_t slot = 0; slot < initial.size(); ++slot)
            result.emplace_back(forward ? forward_added[slot] : forward_removed[slot], forward ? forward_removed[slot] : forward_added[slot]);
        return result;
    }
    /// Updates against full evaluation of the same state.
    void check() const
    {
        auto full = evaluator();
        auto incremental = evaluator();
        incremental.initialize(initial);
        for (const bool forward : { true, false, true })
        {
            incremental.update(changes(forward));
            full.evaluate(forward ? changed : initial);
            for (size_t output = 0; output < workload->outputs.size(); ++output)
            {
                const auto& lhs = incremental.get_result(output);
                const auto& rhs = full.get_result(output);
                bool same = lhs.size() == rhs.size();
                for (size_t i = 0; same && i < lhs.size(); ++i)
                    same = rhs.contains(lhs.row(i));
                if (!same)
                {
                    std::cerr << "Datalog benchmark: incremental and full evaluation disagree.\n";
                    std::abort();
                }
            }
        }
    }
};

template<auto Make>
Prepared& prepared(int64_t size)
{
    static std::map<int64_t, std::unique_ptr<Prepared>> cache;
    auto& entry = cache[size];
    if (!entry)
        entry = std::make_unique<Prepared>(Make(static_cast<uint_t>(size)));
    return *entry;
}

void counters(benchmark::State& state, const Prepared& p, const dl::Evaluator& evaluator)
{
    size_t rows = 0;
    for (size_t output = 0; output < p.workload->outputs.size(); ++output)
        rows += evaluator.get_result(output).size();
    state.counters["input_rows"] = static_cast<double>(p.workload->rows(p.workload->initial));
    state.counters["output_rows"] = static_cast<double>(rows);
}

template<auto Make>
void full_evaluation(benchmark::State& state)
{
    auto& p = prepared<Make>(state.range(0));
    auto evaluator = p.evaluator();
    bool forward = true;
    for (auto _ : state)
    {
        evaluator.evaluate(forward ? p.changed : p.initial);
        forward = !forward;
        benchmark::ClobberMemory();
    }
    counters(state, p, evaluator);
}

template<auto Make>
void update(benchmark::State& state)
{
    auto& p = prepared<Make>(state.range(0));
    auto evaluator = p.evaluator();
    evaluator.initialize(p.initial);
    const auto forward = p.changes(true), backward = p.changes(false);
    bool ahead = false;
    for (auto _ : state)
    {
        evaluator.update(ahead ? backward : forward);
        ahead = !ahead;
        benchmark::ClobberMemory();
    }
    counters(state, p, evaluator);
}

std::unique_ptr<Workload> grid_workload(uint_t n) { return grid(n); }
std::unique_ptr<Workload> termes_workload(uint_t n) { return termes(n, 6); }
std::unique_ptr<Workload> closure_workload(uint_t n) { return closure(n); }
std::unique_ptr<Workload> paths_workload(uint_t n) { return paths(n); }
std::unique_ptr<Workload> triangles_workload(uint_t n) { return triangles(n); }

}  // namespace

BENCHMARK(full_evaluation<grid_workload>)->Name("grid/full")->Arg(8)->Arg(16)->Arg(32)->Unit(benchmark::kMicrosecond);
BENCHMARK(update<grid_workload>)->Name("grid/update")->Arg(8)->Arg(16)->Arg(32)->Unit(benchmark::kMicrosecond);
BENCHMARK(full_evaluation<termes_workload>)->Name("termes/full")->Arg(6)->Arg(12)->Arg(24)->Unit(benchmark::kMicrosecond);
BENCHMARK(update<termes_workload>)->Name("termes/update")->Arg(6)->Arg(12)->Arg(24)->Unit(benchmark::kMicrosecond);
BENCHMARK(full_evaluation<closure_workload>)->Name("closure/full")->Arg(32)->Arg(128)->Arg(512)->Unit(benchmark::kMicrosecond);
BENCHMARK(update<closure_workload>)->Name("closure/update")->Arg(32)->Arg(128)->Arg(512)->Unit(benchmark::kMicrosecond);
BENCHMARK(full_evaluation<paths_workload>)->Name("paths/full")->Arg(32)->Arg(128)->Arg(512)->Unit(benchmark::kMicrosecond);
BENCHMARK(update<paths_workload>)->Name("paths/update")->Arg(32)->Arg(128)->Arg(512)->Unit(benchmark::kMicrosecond);
BENCHMARK(full_evaluation<triangles_workload>)->Name("triangles/full")->Arg(128)->Arg(512)->Arg(2048)->Unit(benchmark::kMicrosecond);
BENCHMARK(update<triangles_workload>)->Name("triangles/update")->Arg(128)->Arg(512)->Arg(2048)->Unit(benchmark::kMicrosecond);
}  // namespace ygg::profiling
