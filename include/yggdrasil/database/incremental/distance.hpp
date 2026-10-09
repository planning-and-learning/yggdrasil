/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_INCREMENTAL_DISTANCE_HPP_
#define YGG_DATABASE_INCREMENTAL_DISTANCE_HPP_

#include "yggdrasil/database/details/distance_repair.hpp"
#include "yggdrasil/database/distance.hpp"
#include "yggdrasil/database/incremental/delta.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ygg::database::incremental
{

/// Maintains every source's distances using Ramalingam and Reps, "An Incremental
/// Algorithm for a Generalization of the Shortest-Path Problem" (1996), Section 4,
/// Figure 4 (DynamicSSF-G), https://doi.org/10.1006/jagm.1996.0046.
/// All edge changes are seeded before repair. Counts represent the paper's support
/// sets; temporary heaps retain only improving edges. See docs/database.md for the
/// unit-edge specialization and the costs outside the per-source repair algorithm.
///
/// Inputs are borrowed during calls only. Added/removed inputs describe actual,
/// disjoint set changes; swapping each pair undoes an update. Schema/overlap errors
/// preserve the previous result. A failure during mutation requires initialize().
/// Results and deltas are borrowed until the next mutation. Tuple IDs are retained
/// until initialize(), so memory follows historical vertices and peak source count.
template<ColumnTypes Values = DefaultColumnTypes>
    requires ColumnValueFor<uint_t, Values>
class DistanceEvaluator
{
    static constexpr uint_t infinity = database::detail::distance_infinity;
    static constexpr size_t absent = std::numeric_limits<size_t>::max();

    enum class Flag : unsigned char
    {
        CLEAR,
        SET
    };

    struct SourceState
    {
        uint_t root = 0;
        std::vector<uint_t> distances;
        std::vector<size_t> supports;
    };

    DistancePlan<Values> m_plan;
    database::detail::DistanceGraph m_graph;
    UnorderedMultiMap<uint_t, uint_t> m_reverse;
    std::vector<SourceState> m_sources;
    size_t m_active_sources = 0;
    std::vector<size_t> m_source_slots;
    std::vector<uint_t> m_targets;
    std::vector<Flag> m_target_flags;
    Builder<Relation<Values>> m_result;
    Delta<Values> m_delta;
    bool m_initialized = false;

    // One scratch area serves all source trees sequentially.
    std::vector<uint_t> m_queue;
    std::unique_ptr<detail::DistanceRepairWorkspace> m_repair = std::make_unique<detail::DistanceRepairWorkspace>();
    std::vector<std::pair<uint_t, uint_t>> m_changed;
    std::vector<Flag> m_changed_flags;
    std::vector<std::byte> m_row;

    std::vector<uint_t> m_sources_added;
    std::vector<uint_t> m_sources_removed;
    std::vector<uint_t> m_targets_added;
    std::vector<uint_t> m_targets_removed;
    std::vector<std::pair<uint_t, uint_t>> m_edges_added;
    std::vector<std::pair<uint_t, uint_t>> m_edges_removed;

    template<RelationViewConcept<Values> V>
    void require_external_input(const V& input) const;
    uint_t require_vertex(std::span<const std::byte> row) const;
    bool is_source(uint_t vertex) const noexcept;
    bool is_target(uint_t vertex) const noexcept;
    void resize_vertices();
    SourceState& add_source(uint_t root);
    void remove_source(uint_t root);
    void insert_result(uint_t source, uint_t target, uint_t length);
    void remove_result(uint_t source, uint_t target, uint_t length);
    void set_distance(SourceState& state, uint_t vertex, uint_t length);
    static uint_t successor_distance(uint_t distance) noexcept;
    void update_edge(SourceState& state, uint_t from, uint_t to, std::optional<uint_t> before, std::optional<uint_t> after);
    void rebuild_incoming(SourceState& state, uint_t vertex);
    void repair(SourceState& state);
    void clear_changes();

public:
    explicit DistanceEvaluator(DistancePlan<Values> plan);

    /// Replace all inputs, retaining buffers, and clear the last output delta.
    template<RelationViewConcept<Values> S, RelationViewConcept<Values> E, RelationViewConcept<Values> T>
    void initialize(const S& sources, const E& edges, const T& targets);

    /// Publish only the net result change of this complete batch.
    template<RelationViewConcept<Values> SA,
             RelationViewConcept<Values> SR,
             RelationViewConcept<Values> EA,
             RelationViewConcept<Values> ER,
             RelationViewConcept<Values> TA,
             RelationViewConcept<Values> TR>
    void update(const SA& sources_added,
                const SR& sources_removed,
                const EA& edges_added,
                const ER& edges_removed,
                const TA& targets_added,
                const TR& targets_removed);

    const Builder<Relation<Values>>& get_result() const&;
    const Builder<Relation<Values>>& get_result() const&& = delete;
    const Delta<Values>& get_delta() const&;
    const Delta<Values>& get_delta() const&& = delete;
    /// Retained storage, including inactive reusable source arrays; excludes the plan.
    size_t memory_usage() const noexcept;
};

template<ColumnTypes Values>
    requires ColumnValueFor<uint_t, Values>
DistanceEvaluator<Values>::DistanceEvaluator(DistancePlan<Values> plan) :
    m_plan(std::move(plan)),
    m_graph(m_plan.tuple_size()),
    m_result(m_plan.output_columns().span()),
    m_delta(m_plan.output_columns().span())
{
}

template<ColumnTypes Values>
    requires ColumnValueFor<uint_t, Values>
template<RelationViewConcept<Values> V>
void DistanceEvaluator<Values>::require_external_input(const V& input) const
{
    const auto address = input.get_storage_address();
    if (address == m_result.get_storage_address() || address == m_delta.added.get_storage_address() || address == m_delta.removed.get_storage_address())
        throw std::invalid_argument("Incremental distance: input aliases evaluator storage.");
}

template<ColumnTypes Values>
    requires ColumnValueFor<uint_t, Values>
uint_t DistanceEvaluator<Values>::require_vertex(std::span<const std::byte> row) const
{
    const auto vertex = m_graph.vertices.find(row);
    if (!vertex)
        throw std::invalid_argument("Incremental distance: removed vertex is absent.");
    return *vertex;
}

template<ColumnTypes Values>
    requires ColumnValueFor<uint_t, Values>
bool DistanceEvaluator<Values>::is_source(uint_t vertex) const noexcept
{
    return vertex < m_source_slots.size() && m_source_slots[vertex] != absent;
}

template<ColumnTypes Values>
    requires ColumnValueFor<uint_t, Values>
bool DistanceEvaluator<Values>::is_target(uint_t vertex) const noexcept
{
    return vertex < m_target_flags.size() && m_target_flags[vertex] == Flag::SET;
}

template<ColumnTypes Values>
    requires ColumnValueFor<uint_t, Values>
void DistanceEvaluator<Values>::resize_vertices()
{
    const auto size = m_graph.vertices.size();
    m_source_slots.resize(size, absent);
    m_target_flags.resize(size, Flag::CLEAR);
    m_repair->resize(size);
    m_changed_flags.resize(size, Flag::CLEAR);
    for (size_t i = 0; i < m_active_sources; ++i)
    {
        m_sources[i].distances.resize(size, infinity);
        m_sources[i].supports.resize(size, 0);
    }
}

template<ColumnTypes Values>
    requires ColumnValueFor<uint_t, Values>
auto DistanceEvaluator<Values>::add_source(uint_t root) -> SourceState&
{
    if (m_active_sources == m_sources.size())
        m_sources.emplace_back();
    auto& state = m_sources[m_active_sources];
    state.root = root;
    database::detail::breadth_first_search(m_graph, root, state.distances, m_queue);
    state.supports.assign(m_graph.vertices.size(), 0);
    state.supports[root] = 1;  // The root has a constant zero-distance contribution.
    // SP includes infinite contributions to unreachable vertices (infinity <= infinity).
    for (uint_t to = 0; to < m_graph.vertices.size(); ++to)
        for (const auto from : m_reverse.values(to))
            if (successor_distance(state.distances[from]) <= state.distances[to])
                ++state.supports[to];
    m_source_slots[root] = m_active_sources++;
    return state;
}

template<ColumnTypes Values>
    requires ColumnValueFor<uint_t, Values>
void DistanceEvaluator<Values>::remove_source(uint_t root)
{
    const auto position = m_source_slots[root];
    assert(position < m_active_sources);
    --m_active_sources;
    if (position != m_active_sources)
    {
        std::swap(m_sources[position], m_sources[m_active_sources]);
        m_source_slots[m_sources[position].root] = position;
    }
    m_source_slots[root] = absent;
}

template<ColumnTypes Values>
    requires ColumnValueFor<uint_t, Values>
void DistanceEvaluator<Values>::insert_result(uint_t source, uint_t target, uint_t length)
{
    if (length == infinity)
        return;
    const auto row = database::detail::make_distance_row(m_graph, source, target, length, m_plan, m_row);
    m_result.insert(row);
    m_delta.added.insert(row);
}

template<ColumnTypes Values>
    requires ColumnValueFor<uint_t, Values>
void DistanceEvaluator<Values>::remove_result(uint_t source, uint_t target, uint_t length)
{
    if (length == infinity)
        return;
    const auto row = database::detail::make_distance_row(m_graph, source, target, length, m_plan, m_row);
    const auto position = m_result.find(row);
    assert(position);
    m_delta.removed.insert(row);
    m_result.erase(*position);
}

template<ColumnTypes Values>
    requires ColumnValueFor<uint_t, Values>
void DistanceEvaluator<Values>::set_distance(SourceState& state, uint_t vertex, uint_t length)
{
    if (m_changed_flags[vertex] == Flag::CLEAR)
    {
        m_changed_flags[vertex] = Flag::SET;
        m_changed.emplace_back(vertex, state.distances[vertex]);
    }
    state.distances[vertex] = length;
}

template<ColumnTypes Values>
    requires ColumnValueFor<uint_t, Values>
uint_t DistanceEvaluator<Values>::successor_distance(uint_t distance) noexcept
{
    return distance == infinity ? infinity : distance + 1;
}

template<ColumnTypes Values>
    requires ColumnValueFor<uint_t, Values>
void DistanceEvaluator<Values>::update_edge(SourceState& state, uint_t from, uint_t to, std::optional<uint_t> before, std::optional<uint_t> after)
{
    // Presence is separate from infinity: deleting an unreachable edge still removes it from SP.
    auto& supports = state.supports[to];
    if (before && successor_distance(*before) <= state.distances[to])
    {
        assert(supports != 0);
        --supports;
    }
    if (after && successor_distance(*after) <= state.distances[to])
        ++supports;
    m_repair->set_edge(from, to, after ? successor_distance(*after) : infinity, state.distances[to]);
    m_repair->schedule(to, state.distances[to], supports);
}

template<ColumnTypes Values>
    requires ColumnValueFor<uint_t, Values>
void DistanceEvaluator<Values>::rebuild_incoming(SourceState& state, uint_t vertex)
{
    assert(m_repair->local[vertex].empty());
    auto& supports = state.supports[vertex];
    supports = vertex == state.root ? 1 : 0;
    for (const auto from : m_reverse.values(vertex))
    {
        detail::count_distance_work(&detail::DistanceWork::incoming_edges);
        const auto candidate = successor_distance(state.distances[from]);
        if (candidate <= state.distances[vertex])
            ++supports;
        m_repair->set_edge(from, vertex, candidate, state.distances[vertex]);
    }
    m_repair->schedule(vertex, state.distances[vertex], supports);
}

template<ColumnTypes Values>
    requires ColumnValueFor<uint_t, Values>
void DistanceEvaluator<Values>::repair(SourceState& state)
{
    assert(m_repair->global.empty() && m_repair->edges.empty());
    // Figure 4: seed the entire mixed batch before extracting any inconsistent vertex.
    for (const auto& [from, to] : m_edges_removed)
        update_edge(state, from, to, state.distances[from], std::nullopt);
    for (const auto& [from, to] : m_edges_added)
        update_edge(state, from, to, std::nullopt, state.distances[from]);

    while (!m_repair->global.empty())
    {
        const auto [key, vertex] = m_repair->pop();
        detail::count_distance_work(&detail::DistanceWork::processed_vertices);
        const auto before = state.distances[vertex];
        const auto after = key < before ? key : infinity;
        set_distance(state, vertex, after);
        if (key < before)
            state.supports[vertex] = m_repair->accept_improvement(vertex, key);
        else
            rebuild_incoming(state, vertex);
        for (const auto to : m_graph.outgoing.values(vertex))
        {
            detail::count_distance_work(&detail::DistanceWork::outgoing_edges);
            update_edge(state, vertex, to, before, after);
        }
    }
    assert(m_repair->edges.empty());
    for (const auto& [target, before] : m_changed)
    {
        m_changed_flags[target] = Flag::CLEAR;
        if (is_target(target) && before != state.distances[target])
        {
            remove_result(state.root, target, before);
            insert_result(state.root, target, state.distances[target]);
        }
    }
    m_changed.clear();
}

template<ColumnTypes Values>
    requires ColumnValueFor<uint_t, Values>
void DistanceEvaluator<Values>::clear_changes()
{
    m_sources_added.clear();
    m_sources_removed.clear();
    m_targets_added.clear();
    m_targets_removed.clear();
    m_edges_added.clear();
    m_edges_removed.clear();
}

template<ColumnTypes Values>
    requires ColumnValueFor<uint_t, Values>
template<RelationViewConcept<Values> S, RelationViewConcept<Values> E, RelationViewConcept<Values> T>
void DistanceEvaluator<Values>::initialize(const S& sources, const E& edges, const T& targets)
{
    database::detail::require_plan_columns(sources.columns().span(), m_plan.source_columns().span());
    database::detail::require_plan_columns(edges.columns().span(), m_plan.edge_columns().span());
    database::detail::require_plan_columns(targets.columns().span(), m_plan.target_columns().span());
    require_external_input(sources);
    require_external_input(edges);
    require_external_input(targets);
    m_initialized = false;
    m_graph.clear();
    m_reverse.clear();
    m_active_sources = 0;
    m_source_slots.clear();
    m_targets.clear();
    m_target_flags.clear();
    m_result.clear();
    m_delta.clear();
    m_queue.clear();
    m_repair->clear();
    m_changed.clear();
    m_changed_flags.clear();
    clear_changes();
    m_graph.outgoing.reserve(edges.size());
    m_reverse.reserve(edges.size());
    for (size_t i = 0; i < edges.size(); ++i)
    {
        const auto row = edges.row(i);
        const auto from = m_graph.insert_vertex(row.first(m_plan.tuple_size()));
        const auto to = m_graph.insert_vertex(row.subspan(m_plan.tuple_size()));
        m_graph.outgoing.insert(from, to);
        m_reverse.insert(to, from);
    }
    for (size_t i = 0; i < sources.size(); ++i)
        m_graph.insert_vertex(sources.row(i));
    for (size_t i = 0; i < targets.size(); ++i)
        m_targets.push_back(m_graph.insert_vertex(targets.row(i)));
    resize_vertices();
    for (const auto target : m_targets)
        m_target_flags[target] = Flag::SET;
    for (size_t i = 0; i < sources.size(); ++i)
    {
        const auto& state = add_source(*m_graph.vertices.find(sources.row(i)));
        for (const auto target : m_targets)
            if (state.distances[target] != infinity)
                m_result.insert(database::detail::make_distance_row(m_graph, state.root, target, state.distances[target], m_plan, m_row));
    }
    m_initialized = true;
}

template<ColumnTypes Values>
    requires ColumnValueFor<uint_t, Values>
template<RelationViewConcept<Values> SA,
         RelationViewConcept<Values> SR,
         RelationViewConcept<Values> EA,
         RelationViewConcept<Values> ER,
         RelationViewConcept<Values> TA,
         RelationViewConcept<Values> TR>
void DistanceEvaluator<Values>::update(const SA& sources_added,
                                       const SR& sources_removed,
                                       const EA& edges_added,
                                       const ER& edges_removed,
                                       const TA& targets_added,
                                       const TR& targets_removed)
{
    if (!m_initialized)
        throw std::logic_error("Incremental distance: initialize before updating.");
    detail::require_delta<Values>(sources_added, sources_removed, m_plan.source_columns().span());
    detail::require_delta<Values>(edges_added, edges_removed, m_plan.edge_columns().span());
    detail::require_delta<Values>(targets_added, targets_removed, m_plan.target_columns().span());
    require_external_input(sources_added);
    require_external_input(sources_removed);
    require_external_input(edges_added);
    require_external_input(edges_removed);
    require_external_input(targets_added);
    require_external_input(targets_removed);
    m_initialized = false;
    m_delta.clear();
    clear_changes();

    for (size_t i = 0; i < sources_removed.size(); ++i)
    {
        const auto id = require_vertex(sources_removed.row(i));
        if (!is_source(id))
            throw std::invalid_argument("Incremental distance: removed source is absent.");
        m_sources_removed.push_back(id);
    }
    for (size_t i = 0; i < targets_removed.size(); ++i)
    {
        const auto id = require_vertex(targets_removed.row(i));
        if (!is_target(id))
            throw std::invalid_argument("Incremental distance: removed target is absent.");
        m_targets_removed.push_back(id);
    }
    for (size_t i = 0; i < edges_removed.size(); ++i)
    {
        const auto row = edges_removed.row(i);
        const auto from = require_vertex(row.first(m_plan.tuple_size()));
        const auto to = require_vertex(row.subspan(m_plan.tuple_size()));
        if (!m_graph.contains_edge(from, to))
            throw std::invalid_argument("Incremental distance: removed edge is absent.");
        m_edges_removed.emplace_back(from, to);
    }
    for (size_t i = 0; i < sources_added.size(); ++i)
    {
        const auto id = m_graph.insert_vertex(sources_added.row(i));
        if (is_source(id))
            throw std::invalid_argument("Incremental distance: added source is already present.");
        m_sources_added.push_back(id);
    }
    for (size_t i = 0; i < targets_added.size(); ++i)
    {
        const auto id = m_graph.insert_vertex(targets_added.row(i));
        if (is_target(id))
            throw std::invalid_argument("Incremental distance: added target is already present.");
        m_targets_added.push_back(id);
    }
    for (size_t i = 0; i < edges_added.size(); ++i)
    {
        const auto row = edges_added.row(i);
        const auto from = m_graph.insert_vertex(row.first(m_plan.tuple_size()));
        const auto to = m_graph.insert_vertex(row.subspan(m_plan.tuple_size()));
        if (m_graph.contains_edge(from, to))
            throw std::invalid_argument("Incremental distance: added edge is already present.");
        m_edges_added.emplace_back(from, to);
    }

    // Remove old source/target pairs before repairing distances, so simultaneous
    // membership changes cannot publish intermediate rows or duplicate removals.
    for (const auto root : m_sources_removed)
    {
        const auto& state = m_sources[m_source_slots[root]];
        for (const auto target : m_targets)
            remove_result(root, target, state.distances[target]);
        remove_source(root);
    }
    for (const auto target : m_targets_removed)
    {
        for (size_t i = 0; i < m_active_sources; ++i)
            remove_result(m_sources[i].root, target, m_sources[i].distances[target]);
        m_target_flags[target] = Flag::CLEAR;
        const auto position = std::ranges::find(m_targets, target);
        assert(position != m_targets.end());
        *position = m_targets.back();
        m_targets.pop_back();
    }
    for (const auto& [from, to] : m_edges_removed)
    {
        [[maybe_unused]] const auto forward = m_graph.outgoing.erase(from, to);
        [[maybe_unused]] const auto reverse = m_reverse.erase(to, from);
        assert(forward && reverse);
    }
    m_graph.outgoing.reserve_values(m_graph.outgoing.size() + m_edges_added.size());
    m_reverse.reserve_values(m_reverse.size() + m_edges_added.size());
    for (const auto& [from, to] : m_edges_added)
    {
        m_graph.outgoing.insert(from, to);
        m_reverse.insert(to, from);
    }
    resize_vertices();
    if (!m_edges_added.empty() || !m_edges_removed.empty())
        for (size_t i = 0; i < m_active_sources; ++i)
            repair(m_sources[i]);
    for (const auto target : m_targets_added)
    {
        m_target_flags[target] = Flag::SET;
        m_targets.push_back(target);
        for (size_t i = 0; i < m_active_sources; ++i)
            insert_result(m_sources[i].root, target, m_sources[i].distances[target]);
    }
    for (const auto root : m_sources_added)
    {
        const auto& state = add_source(root);
        for (const auto target : m_targets)
            insert_result(root, target, state.distances[target]);
    }
    m_initialized = true;
}

template<ColumnTypes Values>
    requires ColumnValueFor<uint_t, Values>
const Builder<Relation<Values>>& DistanceEvaluator<Values>::get_result() const&
{
    if (!m_initialized)
        throw std::logic_error("Incremental distance: result requires a successful evaluation.");
    return m_result;
}

template<ColumnTypes Values>
    requires ColumnValueFor<uint_t, Values>
const Delta<Values>& DistanceEvaluator<Values>::get_delta() const&
{
    if (!m_initialized)
        throw std::logic_error("Incremental distance: delta requires a successful evaluation.");
    return m_delta;
}

template<ColumnTypes Values>
    requires ColumnValueFor<uint_t, Values>
size_t DistanceEvaluator<Values>::memory_usage() const noexcept
{
    size_t bytes =
        m_graph.memory_usage() + m_reverse.memory_usage() + m_result.memory_usage() + m_delta.memory_usage() + (m_repair ? m_repair->memory_usage() : 0);
    bytes += m_sources.capacity() * sizeof(SourceState) + m_source_slots.capacity() * sizeof(size_t);
    for (const auto& source : m_sources)
        bytes += source.distances.capacity() * sizeof(uint_t) + source.supports.capacity() * sizeof(size_t);
    bytes += (m_targets.capacity() + m_queue.capacity() + m_sources_added.capacity() + m_sources_removed.capacity() + m_targets_added.capacity()
              + m_targets_removed.capacity())
             * sizeof(uint_t);
    bytes += (m_changed.capacity() + m_edges_added.capacity() + m_edges_removed.capacity()) * sizeof(std::pair<uint_t, uint_t>);
    bytes += (m_target_flags.capacity() + m_changed_flags.capacity()) * sizeof(Flag) + m_row.capacity();
    return bytes;
}

}  // namespace ygg::database::incremental

#endif
