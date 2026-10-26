/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/datalog/evaluator.hpp"

#include "yggdrasil/database/datalog/details/compile.hpp"
#include "yggdrasil/database/semantics/incremental/query.hpp"
#include "yggdrasil/database/semantics/relation_repository.hpp"

#include <stdexcept>
#include <utility>

namespace ygg::database::datalog
{
struct Evaluator::Impl
{
    /// An input slot: its rows and their change, labeled like the plan's input; a slot the program does not read has no
    /// columns and stays empty.
    struct Slot
    {
        Builder<database::Relation> rows;
        incremental::Delta change;
        bool read = false;
        bool is_static = false;
        bool loaded = false;
        explicit Slot(std::span<const ColumnLayout> columns) : rows(columns), change(columns) {}
    };

    incremental::QueryEvaluator evaluator;
    size_t unit_slot;
    std::vector<Slot> slots;
    RelationRepository context = RelationRepositoryFactory {}.create();
    bool initialized = false;

    Impl(const detail::QueryProgram& program, std::span<const size_t> static_slots) : evaluator(program.plan), unit_slot(program.unit_slot)
    {
        auto columns = std::vector<std::span<const ColumnLayout>>(unit_slot + 1);
        auto read = std::vector<bool>(unit_slot + 1);
        for (size_t i = 0; i < program.plan.node_count(); ++i)
            if (const auto query = program.plan[Index<Query<>>(to_uint_t(i))]; is<QueryInputTag>(query))
            {
                const auto slot = as<QueryInputTag>(query).get_input_slot();
                columns[slot] = query.columns();
                read[slot] = true;
            }
        slots.reserve(columns.size());
        for (size_t slot = 0; slot < columns.size(); ++slot)
            slots.emplace_back(columns[slot]).read = read[slot];
        auto& unit = slots[unit_slot];
        unit.rows.insert(std::span<const std::byte>());
        unit.read = false;
        unit.is_static = true;
        for (const auto slot : static_slots)
            if (slot < unit_slot)
                slots[slot].is_static = true;
    }

    [[nodiscard]] BorrowedRelationView borrow(const Builder<database::Relation>& rows) const { return { rows, context }; }
};

Evaluator::Evaluator(ProgramView program, std::span<const RelationView> outputs, std::span<const size_t> static_slots) :
    m_impl(std::make_unique<Impl>(detail::compile(program, outputs), static_slots))
{
}

Evaluator::~Evaluator() = default;
Evaluator::Evaluator(Evaluator&&) noexcept = default;
Evaluator& Evaluator::operator=(Evaluator&&) noexcept = default;

bool Evaluator::reads(size_t slot) const
{
    if (slot >= m_impl->unit_slot)
        return false;
    const auto& binding = m_impl->slots[slot];
    return binding.read && !(binding.is_static && binding.loaded);
}

Builder<database::Relation>& Evaluator::input(size_t slot)
{
    auto& rows = m_impl->slots[slot].rows;
    rows.clear();
    return rows;
}

incremental::Delta& Evaluator::change(size_t slot)
{
    auto& delta = m_impl->slots[slot].change;
    delta.clear();
    return delta;
}

void Evaluator::finish_initialize()
{
    auto& impl = *m_impl;
    impl.initialized = false;
    auto bindings = std::vector<BorrowedRelationView>();
    bindings.reserve(impl.slots.size());
    for (const auto& slot : impl.slots)
        bindings.push_back(impl.borrow(slot.rows));
    impl.evaluator.initialize(bindings);
    for (auto& slot : impl.slots)
    {
        slot.loaded = true;
        // The query evaluator keeps its own copy; a static input is kept to be bound again by the next initialize.
        if (!slot.is_static)
            slot.rows.clear();
    }
    impl.initialized = true;
}

void Evaluator::finish_update()
{
    auto& impl = *m_impl;
    if (!impl.initialized)
        throw std::logic_error("Datalog evaluator: initialize before update.");
    auto changes = std::vector<std::pair<BorrowedRelationView, BorrowedRelationView>>();
    changes.reserve(impl.slots.size());
    // A slot not read by this update (static, or not given) has an empty change.
    for (const auto& slot : impl.slots)
        changes.emplace_back(impl.borrow(slot.change.added), impl.borrow(slot.change.removed));
    impl.evaluator.update(changes);
    for (auto& slot : impl.slots)
        slot.change.clear();
}

const Builder<database::Relation>& Evaluator::get_result(size_t output) const
{
    if (!m_impl->initialized)
        throw std::logic_error("Datalog evaluator: evaluate before reading results.");
    return m_impl->evaluator.get_result(output);
}

const incremental::Delta& Evaluator::get_delta(size_t output) const
{
    if (!m_impl->initialized)
        throw std::logic_error("Datalog evaluator: evaluate before reading results.");
    return m_impl->evaluator.get_delta(output);
}

size_t Evaluator::memory_usage() const noexcept
{
    size_t result = m_impl->evaluator.memory_usage();
    for (const auto& slot : m_impl->slots)
        result += slot.rows.memory_usage() + slot.change.added.memory_usage() + slot.change.removed.memory_usage();
    return result;
}
}  // namespace ygg::database::datalog
