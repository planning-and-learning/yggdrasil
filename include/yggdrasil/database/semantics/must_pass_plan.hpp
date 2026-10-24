/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_MUST_PASS_PLAN_HPP_
#define YGG_DATABASE_SEMANTICS_MUST_PASS_PLAN_HPP_

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
class MustPassPlan
{
    Builder<Columns> m_sources;
    Builder<Columns> m_edges;
    Builder<Columns> m_targets;
    Builder<Columns> m_probes;
    Builder<Columns> m_output;
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

    [[nodiscard]] auto source_columns() const& noexcept { return make_view(m_sources, *this); }
    [[nodiscard]] auto source_columns() const&& = delete;
    [[nodiscard]] auto edge_columns() const& noexcept { return make_view(m_edges, *this); }
    [[nodiscard]] auto edge_columns() const&& = delete;
    [[nodiscard]] auto target_columns() const& noexcept { return make_view(m_targets, *this); }
    [[nodiscard]] auto target_columns() const&& = delete;
    [[nodiscard]] auto probe_columns() const& noexcept { return make_view(m_probes, *this); }
    [[nodiscard]] auto probe_columns() const&& = delete;
    [[nodiscard]] auto output_columns() const& noexcept { return make_view(m_output, *this); }
    [[nodiscard]] auto output_columns() const&& = delete;
    [[nodiscard]] size_t keys() const noexcept { return m_keys; }
    /// k, the columns of a vertex.
    [[nodiscard]] size_t arity() const noexcept { return m_sources.size() - m_keys; }
    /// The bytes of a key, which precede a vertex in every row.
    [[nodiscard]] size_t key_size() const noexcept { return m_keys == 0 ? 0 : m_sources[m_keys].offset; }
    [[nodiscard]] size_t vertex_size() const noexcept { return m_sources.row_size() - key_size(); }
    [[nodiscard]] size_t probe_size() const noexcept { return m_probes.row_size(); }
    /// Where an edge row's target vertex starts.
    [[nodiscard]] size_t edge_target_offset() const noexcept { return m_edges[m_keys + arity()].offset; }
    /// The byte offsets, within a vertex, of its columns of the probes' type.
    [[nodiscard]] std::vector<size_t> probe_positions() const
    {
        auto result = std::vector<size_t>();
        for (size_t i = m_keys; i < m_sources.size(); ++i)
            if (m_sources[i].type == m_probes[0].type)
                result.push_back(m_sources[i].offset - key_size());
        return result;
    }

    static constexpr auto cista_members() noexcept
    {
        return ygg::Members<&MustPassPlan::m_sources,
                            &MustPassPlan::m_edges,
                            &MustPassPlan::m_targets,
                            &MustPassPlan::m_probes,
                            &MustPassPlan::m_output,
                            &MustPassPlan::m_keys> {};
    }
};

}  // namespace ygg::database

#endif
