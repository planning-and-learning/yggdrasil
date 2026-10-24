# Copyright (C) 2026 Dominik Drexler
# SPDX-License-Identifier: GPL-3.0-or-later

"""Diagnostics with source locations, as parsers raise them."""

from pyyggdrasil._pyyggdrasil.diagnostics import Diagnostic as Diagnostic
from pyyggdrasil._pyyggdrasil.diagnostics import DiagnosticNote as DiagnosticNote
from pyyggdrasil._pyyggdrasil.diagnostics import Source as Source
from pyyggdrasil._pyyggdrasil.diagnostics import SourceSpan as SourceSpan
from pyyggdrasil._pyyggdrasil.diagnostics import format_diagnostic as format_diagnostic

__all__ = ["Diagnostic", "DiagnosticNote", "Source", "SourceSpan", "format_diagnostic"]
