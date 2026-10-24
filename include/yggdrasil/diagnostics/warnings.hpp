/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DIAGNOSTICS_WARNINGS_HPP_
#define YGG_DIAGNOSTICS_WARNINGS_HPP_

#include "yggdrasil/diagnostics/diagnostic.hpp"

#include <span>
#include <utility>
#include <vector>

namespace ygg::diagnostics
{
/// Warnings collected by one operation; the caller decides how to report them. yggdrasil is header-only, so there is
/// no process-wide handler: one would exist once per shared library and not reach across Python extension modules.
class Warnings
{
public:
    void add(Diagnostic warning) { m_warnings.push_back(std::move(warning)); }
    [[nodiscard]] std::span<const Diagnostic> get() const noexcept { return m_warnings; }
    [[nodiscard]] bool empty() const noexcept { return m_warnings.empty(); }

private:
    std::vector<Diagnostic> m_warnings;
};
}  // namespace ygg::diagnostics

#endif
