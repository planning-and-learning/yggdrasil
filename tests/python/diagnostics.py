#!/usr/bin/env python3
import gc
import importlib.util
import shutil
import sys
import tempfile
from pathlib import Path


def main() -> None:
    if len(sys.argv) != 4:
        raise SystemExit("usage: diagnostics.py <package-init> <provider-extension> <test-extension>")

    package_init, provider, extension = (Path(argument).resolve() for argument in sys.argv[1:])
    with tempfile.TemporaryDirectory(prefix="pyyggdrasil-diagnostics-") as temporary:
        package_dir = Path(temporary) / "pyyggdrasil"
        package_dir.mkdir()
        shutil.copy2(package_init, package_dir / "__init__.py")
        shutil.copy2(provider, package_dir / provider.name)
        for name in ("diagnostics", "execution"):
            (package_dir / name).mkdir()
            shutil.copy2(package_init.parent / name / "__init__.py", package_dir / name / "__init__.py")

        sys.path.insert(0, temporary)
        try:
            from pyyggdrasil import diagnostics

            assert Path(diagnostics.__file__).parent == package_dir / "diagnostics"
            spec = importlib.util.spec_from_file_location("yggdrasil_diagnostics_test", extension)
            if spec is None or spec.loader is None:
                raise RuntimeError(f"cannot load {extension}")
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)

            assert issubclass(module.ParserError, RuntimeError)
            assert issubclass(module.SemanticError, module.ParserError)
            for error_type in (module.ParserError, module.SemanticError):
                for arguments in ((), ("manual failure",)):
                    error = error_type(*arguments)
                    assert isinstance(error.diagnostic, diagnostics.Diagnostic)
                    assert error.diagnostic.message == str(error)
                    assert error.diagnostic.location is None
                    assert error.diagnostic.notes == []

            try:
                module.raise_base()
            except module.ParserError as error:
                assert type(error) is module.ParserError
                assert error.diagnostic.message == "base failure"
                assert error.diagnostic.location is None
                assert error.diagnostic.notes == []
                assert str(error) == diagnostics.format_diagnostic(error.diagnostic)
            else:
                raise AssertionError("base exception was not raised")

            captured = None
            try:
                module.raise_semantic()
            except module.SemanticError as error:
                assert type(error) is module.SemanticError
                assert isinstance(error, module.ParserError)
                captured = error.diagnostic
                assert str(error) == diagnostics.format_diagnostic(captured)
                try:
                    error.diagnostic = diagnostics.Diagnostic("replacement")
                except AttributeError:
                    pass
                else:
                    raise AssertionError("exception diagnostic must be read-only")
            else:
                raise AssertionError("derived exception was not raised")

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
            for owner, name, value in (
                (captured, "message", "replacement"),
                (captured, "notes", []),
                (first, "message", "replacement"),
                (captured.location, "begin", 0),
                (captured.location.source, "text", "replacement"),
            ):
                try:
                    setattr(owner, name, value)
                except AttributeError:
                    pass
                else:
                    raise AssertionError(f"{name} must be read-only")

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
            assert returned.location.source.filename == "external.policy"

            try:
                module.raise_unrelated()
            except RuntimeError as error:
                assert type(error) is RuntimeError
                assert str(error) == "unrelated failure"
                assert not hasattr(error, "diagnostic")
            else:
                raise AssertionError("unrelated exception was not raised")
        finally:
            sys.path.remove(temporary)


if __name__ == "__main__":
    main()
