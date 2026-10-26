/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_DATALOG_EVALUATOR_HPP_
#define YGG_DATABASE_DATALOG_EVALUATOR_HPP_

#include "yggdrasil/core/byte_codec.hpp"
#include "yggdrasil/database/datalog/repository.hpp"
#include "yggdrasil/database/semantics/incremental/delta.hpp"
#include "yggdrasil/database/semantics/relation_builder.hpp"
#include "yggdrasil/database/semantics/relation_view.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace ygg::database::datalog
{
/// Evaluates the requested relations of a stratified program, fully or incrementally, by compiling it into a query DAG
/// (details/compile.hpp) maintained by incremental::QueryEvaluator, following DBSP [Budiu23]: relations without
/// recursion are queries over their rules, a recursive component is a fixpoint whose updates are exact for additions
/// and removals alike (nested streams, §6), and aggregates are maintained per group. Only what the outputs depend on is
/// compiled, and only the input slots it reads are read.
///
/// Columns are uint_t; relation i of the outputs has columns labeled 0, ..., arity − 1, and an input is read
/// positionally. Input slot s is binding s; static slots are read by the first evaluate() or initialize() only.
///
/// [Budiu23] M. Budiu, F. McSherry, L. Ryzhyk, V. Tannen, DBSP: Automatic Incremental View Maintenance for Rich Query
///   Languages, PVLDB 16(7), 2023.
class Evaluator
{
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    [[nodiscard]] bool reads(size_t slot) const;
    /// The rows of an input slot, cleared to be read anew.
    Builder<database::Relation>& input(size_t slot);
    /// The change of an input slot, cleared to be read anew.
    incremental::Delta& change(size_t slot);
    void finish_initialize();
    void finish_update();

    /// Appends the rows of `relation` to `out` positionally: column c of `relation` is column c of `out`.
    template<RelationViewConcept V>
    static void copy_rows(const V& relation, Builder<database::Relation>& out)
    {
        const auto from = relation.columns().span();
        const auto to = out.columns().span();
        if (from.size() != to.size())
            throw std::invalid_argument("Datalog evaluator: an input does not have its relation's arity.");
        for (const auto& column : from)
            if (column.type != type_id<uint_t>)
                throw std::invalid_argument("Datalog evaluator: input columns must be uint_t.");
        auto row = std::vector<std::byte>(to.size() * sizeof(uint_t));
        for (size_t i = 0; i < relation.size(); ++i)
        {
            const auto bytes = relation.row(i);
            for (size_t c = 0; c < to.size(); ++c)
                std::ranges::copy(bytes.subspan(from[c].offset, sizeof(uint_t)), std::span(row).subspan(to[c].offset).begin());
            out.insert(std::span<const std::byte>(row));
        }
    }
    template<RelationViewRange R>
    void read_inputs(const R& inputs)
    {
        for (size_t slot = 0; slot < std::ranges::size(inputs); ++slot)
            if (reads(slot))
                copy_rows(inputs[slot], input(slot));
    }

public:
    /// Evaluates the requested relations of a program; static slots hold the same rows in every call.
    Evaluator(ProgramView program, std::span<const RelationView> outputs, std::span<const size_t> static_slots = {});
    ~Evaluator();
    Evaluator(const Evaluator&) = delete;
    Evaluator& operator=(const Evaluator&) = delete;
    Evaluator(Evaluator&&) noexcept;
    Evaluator& operator=(Evaluator&&) noexcept;

    /// Full evaluation of the inputs, which update() can then maintain.
    template<RelationViewRange R>
    void evaluate(const R& inputs)
    { initialize(inputs); }
    template<RelationViewRange R>
    void initialize(const R& inputs)
    {
        read_inputs(inputs);
        finish_initialize();
    }
    /// One exact (added, removed) change per input slot; static slots must not change.
    template<incremental::RelationDeltaRange R>
    void update(const R& changes)
    {
        for (size_t slot = 0; slot < std::ranges::size(changes); ++slot)
            if (reads(slot))
            {
                const auto& [added, removed] = changes[slot];
                auto& delta = change(slot);
                copy_rows(added, delta.added);
                copy_rows(removed, delta.removed);
            }
        finish_update();
    }

    [[nodiscard]] const Builder<database::Relation>& get_result(size_t output = 0) const;
    /// The last update's exact change of an output.
    [[nodiscard]] const incremental::Delta& get_delta(size_t output = 0) const;
    [[nodiscard]] size_t memory_usage() const noexcept;
};
}  // namespace ygg::database::datalog

#endif
