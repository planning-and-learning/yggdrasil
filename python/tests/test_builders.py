# Copyright (C) 2026 Dominik Drexler
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builders bound with urd::python::bind_builder: changed by their listed mutators, read through view(context)."""

import gc

from pyyggdrasil import formalism as f


def test_a_relation_builder_inserts_in_tuple_order_and_reads_through_its_view() -> None:
    repository = f.RelationRepository()
    records = f.RowSchemaRecords()
    columns = [f.ColumnLayout(f.ColumnIndex(0), f.ColumnType.UINT32), f.ColumnLayout(f.ColumnIndex(1), f.ColumnType.UINT32)]
    relation = f.RelationSchemaBuilder()
    relation.initialize(repository, repository.schema([column.type for column in columns]), columns)
    assert relation.insert(repository, records, [2, 1]) == (0, True)
    assert relation.insert(repository, records, [1, 5]) == (0, True)
    assert relation.insert(repository, records, [1, 5]) == (0, False)
    assert relation.erase(repository, [2, 1])
    view = relation.view(repository)
    assert isinstance(view, f.RelationSchemaBuilderBorrowed)
    assert view.arity() == 2
    assert len(view.get_rows()) == 1
    assert view.contains([1, 5])
    assert not hasattr(relation, "get_rows")


def test_a_schema_builder_is_set_from_its_records_form() -> None:
    repository = f.RelationRepository()
    schema = f.SchemaBuilder()
    schema.set_column_types(repository, [f.ColumnType.UINT32, f.ColumnType.BOOL])
    view = schema.view(repository)
    assert isinstance(view, f.SchemaBuilderBorrowed)
    assert list(view.get_column_types()) == [f.ColumnType.UINT32, f.ColumnType.BOOL]


def test_a_relation_builder_keeps_its_repository_and_records_alive() -> None:
    repository = f.RelationRepository()
    records = f.RowSchemaRecords()
    columns = [f.ColumnLayout(f.ColumnIndex(0), f.ColumnType.UINT32)]
    relation = f.RelationSchemaBuilder()
    relation.initialize(repository, repository.schema([f.ColumnType.UINT32]), columns)
    relation.insert(repository, records, [3])
    del records
    gc.collect()
    assert relation.view(repository).contains([3])
    del repository
    gc.collect()
    del relation
    gc.collect()
