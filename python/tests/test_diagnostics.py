import gc

import pytest

import pyyggdrasil
import pyyggdrasil.diagnostics as diagnostics
from pyyggdrasil.diagnostics import (
    Diagnostic,
    DiagnosticNote,
    Source,
    SourceSpan,
    format_diagnostic,
)


def test_diagnostics_submodule_is_public() -> None:
    assert diagnostics is pyyggdrasil.diagnostics
    assert diagnostics.__all__ == [
        "Diagnostic", "DiagnosticNote", "Source", "SourceSpan", "format_diagnostic"
    ]
    for name in diagnostics.__all__:
        assert hasattr(diagnostics, name)


def test_source_span_exposes_byte_offsets_and_one_based_location() -> None:
    source = Source("first\né\tvalue\n", "policy.txt", tab_width=8)
    begin = len("first\né\t".encode("utf-8"))
    span = SourceSpan(source, begin, begin + len("value"))

    assert source.text == "first\né\tvalue\n"
    assert source.filename == "policy.txt"
    assert source.tab_width == 8
    assert span.source is source
    assert (span.begin, span.end, span.line, span.column) == (9, 14, 2, 4)

    end = len(source.text.encode("utf-8"))
    point = SourceSpan(source, end, end)
    assert (point.begin, point.end, point.line, point.column) == (15, 15, 3, 1)


def test_defaults_and_locationless_messages() -> None:
    source = Source("")
    point = SourceSpan(source, 0, 0)
    assert source.filename == ""
    assert source.tab_width == 4
    assert (point.line, point.column) == (1, 1)

    note = DiagnosticNote("Try a closing parenthesis")
    assert note.location is None
    diagnostic = Diagnostic("Invalid policy", notes=[note])
    assert diagnostic.message == "Invalid policy"
    assert diagnostic.location is None
    assert diagnostic.notes[0].message == note.message
    assert "Invalid policy" in str(diagnostic)
    assert "Try a closing parenthesis" in str(diagnostic)
    assert str(diagnostic) == format_diagnostic(diagnostic)
    assert Diagnostic("Another error").notes == []


def test_diagnostic_retains_sources_and_notes_from_other_files() -> None:
    def make_diagnostic() -> Diagnostic:
        source = Source("unexpected", "problem.pddl")
        definition = Source("definition", "domain.pddl")
        return Diagnostic(
            "Invalid reference",
            SourceSpan(source, 0, len(source.text)),
            [DiagnosticNote("Defined here", SourceSpan(definition, 0, len(definition.text)))],
        )

    diagnostic = make_diagnostic()
    gc.collect()
    assert diagnostic.location is not None
    assert diagnostic.location.source.text == "unexpected"
    note = diagnostic.notes[0]
    assert note.location is not None
    assert note.location.source.text == "definition"
    rendered = format_diagnostic(diagnostic)
    for fragment in ("Invalid reference", "problem.pddl", "unexpected", "Defined here", "domain.pddl", "definition"):
        assert fragment in rendered
    assert str(diagnostic) == rendered

    source = note.location.source
    del diagnostic, note
    gc.collect()
    assert source.filename == "domain.pddl"
    assert source.text == "definition"


def test_diagnostic_properties_are_read_only_and_notes_are_copied() -> None:
    source = Source("text")
    span = SourceSpan(source, 0, 4)
    note = DiagnosticNote("note", span)
    diagnostic = Diagnostic("message", span, [note])
    for instance, attribute in (
        (source, "text"), (source, "filename"), (source, "tab_width"),
        (span, "source"), (span, "begin"), (span, "end"), (span, "line"), (span, "column"),
        (note, "message"), (note, "location"),
        (diagnostic, "message"), (diagnostic, "location"), (diagnostic, "notes"),
    ):
        with pytest.raises(AttributeError):
            setattr(instance, attribute, None)
    returned_notes = diagnostic.notes
    returned_notes.clear()
    assert len(diagnostic.notes) == 1


@pytest.mark.parametrize("tab_width", [0, -1])
def test_source_rejects_invalid_tab_width(tab_width: int) -> None:
    with pytest.raises(ValueError):
        Source("text", tab_width=tab_width)


@pytest.mark.parametrize(("begin", "end"), [(2, 1), (0, 5), (5, 5)])
def test_source_span_rejects_invalid_ranges(begin: int, end: int) -> None:
    with pytest.raises(ValueError):
        SourceSpan(Source("text"), begin, end)


@pytest.mark.parametrize(("begin", "end"), [(-1, 0), (0, -1)])
def test_source_span_rejects_negative_offsets(begin: int, end: int) -> None:
    with pytest.raises(TypeError):
        SourceSpan(Source("text"), begin, end)
