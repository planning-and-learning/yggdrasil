#!/usr/bin/env python3
# Copyright (C) 2026 Dominik Drexler
# SPDX-License-Identifier: GPL-3.0-or-later
import gc
import importlib.util
import shutil
import sys
import tempfile
from pathlib import Path
from typing import Any, Protocol, cast


def main() -> None:
    if len(sys.argv) != 4:
        msg = "usage: diagnostics.py <package-init> <provider-extension> <test-extension>"
        raise SystemExit(msg)

    package_init, provider, extension = (Path(argument).resolve() for argument in sys.argv[1:])
    with tempfile.TemporaryDirectory(prefix="pyyggdrasil-diagnostics-") as temporary:
        package_dir = Path(temporary) / "pyyggdrasil"
        shutil.copytree(package_init.parent, package_dir, ignore=shutil.ignore_patterns("__pycache__"))
        shutil.copy2(provider, package_dir / provider.name)

        sys.path.insert(0, temporary)
        try:
            # Imported only now, from the copy just put on sys.path.
            from pyyggdrasil import diagnostics

            class DiagnosticError(Protocol):
                """A bound exception type carries its diagnostic as a read-only property set by the bindings."""

                @property
                def diagnostic(self) -> diagnostics.Diagnostic: ...

            def diagnostic_of(error: BaseException) -> diagnostics.Diagnostic:
                return cast("DiagnosticError", error).diagnostic

            assert Path(diagnostics.__file__).parent == package_dir / "diagnostics"
            spec = importlib.util.spec_from_file_location("yggdrasil_diagnostics_test", extension)
            if spec is None or spec.loader is None:
                msg = f"cannot load {extension}"
                raise RuntimeError(msg)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)

            assert issubclass(module.ParserError, RuntimeError)
            assert issubclass(module.SemanticError, module.ParserError)
            for error_type in (module.ParserError, module.SemanticError):
                for arguments in ((), ("manual failure",)):
                    error = error_type(*arguments)
                    assert isinstance(diagnostic_of(error), diagnostics.Diagnostic)
                    assert diagnostic_of(error).message == str(error)
                    assert diagnostic_of(error).location is None
                    assert diagnostic_of(error).notes == []

            try:
                module.raise_base()
            except module.ParserError as error:
                assert type(error) is module.ParserError
                assert diagnostic_of(error).message == "base failure"
                assert diagnostic_of(error).location is None
                assert diagnostic_of(error).notes == []
                assert str(error) == diagnostics.format_diagnostic(diagnostic_of(error))
            else:
                msg = "base exception was not raised"
                raise AssertionError(msg)

            captured = None
            try:
                module.raise_semantic()
            except module.SemanticError as error:
                assert type(error) is module.SemanticError
                assert isinstance(error, module.ParserError)
                captured = diagnostic_of(error)
                assert str(error) == diagnostics.format_diagnostic(captured)
                try:
                    cast("Any", error).diagnostic = diagnostics.Diagnostic("replacement")
                except AttributeError:
                    pass
                else:
                    msg = "exception diagnostic must be read-only"
                    raise AssertionError(msg)
            else:
                msg = "derived exception was not raised"
                raise AssertionError(msg)

            gc.collect()
            assert isinstance(captured, diagnostics.Diagnostic)
            assert captured.message == "invalid second declaration"
            assert captured.location is not None
            assert (captured.location.begin, captured.location.end) == (6, 12)
            assert (captured.location.line, captured.location.column) == (2, 1)
            assert captured.location.source.filename == "temporary.policy"
            assert len(captured.notes) == 2
            first, related = captured.notes
            assert first.message == "first declaration"
            assert first.location is not None
            assert (first.location.begin, first.location.end) == (0, 5)
            assert related.message == "related declaration"
            assert related.location is not None
            assert related.location.source.filename == "related.policy"
            read_only: tuple[tuple[object, str, object], ...] = (
                (captured, "message", "replacement"),
                (captured, "notes", []),
                (first, "message", "replacement"),
                (captured.location, "begin", 0),
                (captured.location.source, "text", "replacement"),
            )
            for owner, name, value in read_only:
                try:
                    setattr(owner, name, value)
                except AttributeError:
                    pass
                else:
                    msg = f"{name} must be read-only"
                    raise AssertionError(msg)

            source = captured.location.source
            del captured
            gc.collect()
            assert source.text == "first\nsecond\n"
            assert related.location.source.text == "related"

            span = diagnostics.SourceSpan(diagnostics.Source("external", "external.policy"), 1, 4)
            returned_span = module.roundtrip_span(span)
            assert isinstance(returned_span, diagnostics.SourceSpan)
            assert returned_span.source.text == "external"
            assert (returned_span.begin, returned_span.end) == (1, 4)
            returned = module.roundtrip_diagnostic(diagnostics.Diagnostic("external diagnostic", span))
            assert isinstance(returned, diagnostics.Diagnostic)
            assert returned.message == "external diagnostic"
            assert returned.location is not None
            assert returned.location.source.filename == "external.policy"

            try:
                module.raise_unrelated()
            except RuntimeError as error:
                assert type(error) is RuntimeError
                assert str(error) == "unrelated failure"
                assert not hasattr(error, "diagnostic")
            else:
                msg = "unrelated exception was not raised"
                raise AssertionError(msg)
        finally:
            sys.path.remove(temporary)


if __name__ == "__main__":
    main()
