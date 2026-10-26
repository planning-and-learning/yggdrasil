/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/datalog/evaluator.hpp"

#include "yggdrasil/database/datalog/detail/executor.hpp"
#include "yggdrasil/database/datalog/detail/program.hpp"

#include <algorithm>
#include <stdexcept>

namespace ygg::database::datalog
{
using namespace detail;

struct Evaluator::Impl
{
    CompiledProgram program;
    Executor executor;
    std::vector<Builder<database::Relation>> results;
    std::vector<Delta> deltas;
    bool ready = false;
    bool initialized = false;
    std::vector<std::byte> bytes;

    Impl(ProgramView view, std::span<const RelationView> outputs, std::span<const size_t> static_slots) :
        program(translate(view, outputs, static_slots)),
        executor(program)
    {
        for (const auto table : program.outputs)
        {
            auto labels = std::vector<Index<Column>>();
            for (size_t i = 0; i < program.tables[table]->arity; ++i)
                labels.emplace_back(to_uint_t(i));
            results.emplace_back(std::span<const Index<Column>>(labels));
            deltas.emplace_back(std::span<const Index<Column>>(labels));
        }
    }

    /// Full evaluation; `maintained` keeps what update() needs.
    void evaluate(bool maintained)
    {
        for (auto& table : program.tables)
            if (table->slot)
                table->loaded = true;
        for (const auto& stratum : program.strata)
        {
            if (maintained)
                stratum->initialize(program, executor);
            else
                stratum->evaluate(program, executor);
        }
        publish(false);
    }

    void update()
    {
        for (const auto& stratum : program.strata)
            if (std::ranges::any_of(stratum->info().reads, [&](size_t t) { return program.tables[t]->changed(); }))
                stratum->update(program, executor);
        publish(true);
    }

    std::span<const std::byte> encode(std::span<const uint_t> row)
    {
        bytes.resize(row.size() * ByteCodec<uint_t>::size);
        for (size_t c = 0; c < row.size(); ++c)
            ByteCodec<uint_t>::encode(row[c], std::span(bytes).subspan(c * ByteCodec<uint_t>::size, ByteCodec<uint_t>::size));
        return bytes;
    }
    void publish(bool exact_changes)
    {
        for (size_t i = 0; i < program.outputs.size(); ++i)
        {
            const auto& table = *program.tables[program.outputs[i]];
            auto& result = results[i];
            auto& delta = deltas[i];
            delta.clear();
            if (!exact_changes)
            {
                result.clear();
                table.current.for_each([&](std::span<const uint_t> row) { result.insert(encode(row)); });
                continue;
            }
            table.removed.for_each(
                [&](std::span<const uint_t> row)
                {
                    const auto encoded = encode(row);
                    delta.removed.insert(encoded);
                    result.erase(*result.find(encoded));
                });
            table.added.for_each(
                [&](std::span<const uint_t> row)
                {
                    const auto encoded = encode(row);
                    delta.added.insert(encoded);
                    result.insert(encoded);
                });
        }
    }
};

Evaluator::Evaluator(ProgramView program, std::span<const RelationView> outputs, std::span<const size_t> static_slots) :
    m_impl(std::make_unique<Impl>(program, outputs, static_slots))
{
}

Evaluator::~Evaluator() = default;
Evaluator::Evaluator(Evaluator&&) noexcept = default;
Evaluator& Evaluator::operator=(Evaluator&&) noexcept = default;

bool Evaluator::reads(size_t slot) const
{
    const auto& slots = m_impl->program.slot_tables;
    if (slot >= slots.size() || slots[slot] == npos)
        return false;
    const auto& table = *m_impl->program.tables[slots[slot]];
    return !(table.is_static && table.loaded);
}

size_t Evaluator::input_arity(size_t slot) const { return m_impl->program.tables[m_impl->program.slot_tables[slot]]->arity; }

void Evaluator::begin(bool updating)
{
    auto& impl = *m_impl;
    if (updating && !impl.initialized)
        throw std::logic_error("Datalog evaluator: initialize before update.");
    impl.ready = false;
    for (auto& table : impl.program.tables)
    {
        table->clear_changes();
        if (!updating && table->slot && !(table->is_static && table->loaded))
            table->current.clear();
    }
}

void Evaluator::input_row(size_t slot, std::span<const uint_t> row) { m_impl->program.tables[m_impl->program.slot_tables[slot]]->current.insert(row); }

void Evaluator::change_row(size_t slot, bool added, std::span<const uint_t> row)
{
    auto& table = *m_impl->program.tables[m_impl->program.slot_tables[slot]];
    if (added)
    {
        if (!table.current.insert(row))
            throw std::invalid_argument("Datalog evaluator: an added row is already present.");
        table.added.insert(row);
    }
    else
    {
        if (!table.current.erase(row))
            throw std::invalid_argument("Datalog evaluator: a removed row is absent.");
        table.removed.insert(row);
    }
}

void Evaluator::finish_evaluate()
{
    m_impl->initialized = false;
    m_impl->evaluate(false);
    m_impl->ready = true;
}

void Evaluator::finish_initialize()
{
    m_impl->evaluate(true);
    m_impl->initialized = true;
    m_impl->ready = true;
}

void Evaluator::finish_update()
{
    if (!m_impl->initialized)
        throw std::logic_error("Datalog evaluator: initialize before update.");
    m_impl->update();
    m_impl->ready = true;
}

const Builder<database::Relation>& Evaluator::get_result(size_t output) const
{
    if (!m_impl->ready)
        throw std::logic_error("Datalog evaluator: evaluate before reading results.");
    return m_impl->results.at(output);
}

const Delta& Evaluator::get_delta(size_t output) const
{
    if (!m_impl->ready)
        throw std::logic_error("Datalog evaluator: evaluate before reading results.");
    return m_impl->deltas.at(output);
}

size_t Evaluator::memory_usage() const noexcept
{
    size_t result = 0;
    for (const auto& table : m_impl->program.tables)
        result += table->memory_usage();
    for (const auto& stratum : m_impl->program.strata)
        result += stratum->memory_usage();
    for (const auto& output : m_impl->results)
        result += output.memory_usage();
    return result;
}
}  // namespace ygg::database::datalog
