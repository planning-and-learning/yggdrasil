# Copyright (C) 2026 Dominik Drexler
# SPDX-License-Identifier: GPL-3.0-or-later

"""Diagnostics with source locations, as parsers raise them."""

from pyyggdrasil._pyyggdrasil.diagnostics import (
    Diagnostic as Diagnostic,
    DiagnosticNote as DiagnosticNote,
    Source as Source,
    SourceSpan as SourceSpan,
    format_diagnostic as format_diagnostic,
)

__all__ = ["Diagnostic", "DiagnosticNote", "Source", "SourceSpan", "format_diagnostic"]
