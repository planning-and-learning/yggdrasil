/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_MUST_PASS_PLAN_HPP_
#define YGG_DATABASE_SEMANTICS_MUST_PASS_PLAN_HPP_

#include "yggdrasil/database/semantics/relation_view.hpp"
#include "yggdrasil/database/syntax/columns.hpp"

#include <cstddef>
#include <span>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace ygg::database
{

/// The columns of a keyed must-pass computation. Sources, edges and targets start with the same `keys` key columns,
/// followed by vertices of k columns: sources have the key and a vertex, edges the key and two vertices (from, to),
/// targets the key and a vertex. Probes have one column, the objects tested; a vertex contains a probe object if one of
/// its columns of the probe's type holds it. Field types must agree by position, independently of column labels. The
/// output has the key columns followed by the probe column.
template<ColumnTypes Values = DefaultColumnTypes>
class MustPassPlan
{
    Builder<Columns<Values>> m_sources;
    Builder<Columns<Values>> m_edges;
    Builder<Columns<Values>> m_targets;
    Builder<Columns<Values>> m_probes;
    Builder<Columns<Values>> m_output;
    size_t m_keys = 0;

public:
    MustPassPlan() = default;
    MustPassPlan(std::span<const ColumnLayout> sources,
                 std::span<const ColumnLayout> edges,
                 std::span<const ColumnLayout> targets,
                 std::span<const ColumnLayout> probes,
                 size_t keys) :
        m_sources(sources),
        m_edges(edges),
        m_targets(targets),
        m_probes(probes),
        m_keys(keys)
    {
        if (m_sources.size() <= keys || m_targets.size() != m_sources.size() || m_edges.size() != keys + 2 * arity())
            throw std::invalid_argument("Must-pass: input arities must be keys + k, keys + 2k, keys + k with k > 0.");
        if (m_probes.size() != 1)
            throw std::invalid_argument("Must-pass: probes must have one column.");
        for (size_t i = 0; i < keys; ++i)
            if (m_sources[i].type != m_edges[i].type || m_sources[i].type != m_targets[i].type)
                throw std::invalid_argument("Must-pass: key field types must agree by position.");
        for (size_t i = keys; i < m_sources.size(); ++i)
            if (m_sources[i].type != m_targets[i].type || m_sources[i].type != m_edges[i].type || m_sources[i].type != m_edges[i + arity()].type)
                throw std::invalid_argument("Must-pass: vertex field types must agree by position.");
        if (probe_positions().empty())
            throw std::invalid_argument("Must-pass: no vertex column has the probes' field type.");
        for (size_t i = 0; i < keys; ++i)
            m_output.push_back(m_sources[i].label, m_sources[i].type);
        m_output.push_back(m_probes[0].label, m_probes[0].type);
    }

    auto source_columns() const& noexcept { return make_view(m_sources, *this); }
    auto source_columns() const&& = delete;
    auto edge_columns() const& noexcept { return make_view(m_edges, *this); }
    auto edge_columns() const&& = delete;
    auto target_columns() const& noexcept { return make_view(m_targets, *this); }
    auto target_columns() const&& = delete;
    auto probe_columns() const& noexcept { return make_view(m_probes, *this); }
    auto probe_columns() const&& = delete;
    auto output_columns() const& noexcept { return make_view(m_output, *this); }
    auto output_columns() const&& = delete;
    size_t keys() const noexcept { return m_keys; }
    /// k, the columns of a vertex.
    size_t arity() const noexcept { return m_sources.size() - m_keys; }
    /// The bytes of a key, which precede a vertex in every row.
    size_t key_size() const noexcept { return m_keys == 0 ? 0 : m_sources[m_keys].offset; }
    size_t vertex_size() const noexcept { return m_sources.row_size() - key_size(); }
    size_t probe_size() const noexcept { return m_probes.row_size(); }
    /// Where an edge row's target vertex starts.
    size_t edge_target_offset() const noexcept { return m_edges[m_keys + arity()].offset; }
    /// The byte offsets, within a vertex, of its columns of the probes' type.
    std::vector<size_t> probe_positions() const
    {
        auto result = std::vector<size_t>();
        for (size_t i = m_keys; i < m_sources.size(); ++i)
            if (m_sources[i].type == m_probes[0].type)
                result.push_back(m_sources[i].offset - key_size());
        return result;
    }

    auto cista_members() noexcept { return std::tie(m_sources, m_edges, m_targets, m_probes, m_output, m_keys); }
    auto cista_members() const noexcept { return std::tie(m_sources, m_edges, m_targets, m_probes, m_output, m_keys); }
};

}  // namespace ygg::database

#endif
