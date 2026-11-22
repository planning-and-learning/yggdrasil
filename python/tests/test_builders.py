# Copyright (C) 2026 Dominik Drexler
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builders bound with urd::python::bind_builder: changed by their listed mutators, read through view(context)."""

import gc

from pyyggdrasil import formalism as f


def test_a_relation_builder_inserts_in_tuple_order_and_reads_through_its_view() -> None:
    repository = f.RelationRepository()
    records = f.RowSchemaRecords()
    columns = [
        f.ColumnLayoutData(f.ColumnIndex(0), f.ColumnType.UINT32),
        f.ColumnLayoutData(f.ColumnIndex(1), f.ColumnType.UINT32),
    ]
    relation = f.RelationSchemaBuilder()
    relation.initialize(repository, repository.insert(f.SchemaData([column.type for column in columns]))[0].index(), columns)
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


def test_a_free_relations_row_resolves_no_objects() -> None:
    repository = f.RelationRepository()
    records = f.RowSchemaRecords()
    columns = [f.ColumnLayoutData(f.ColumnIndex(0), f.ColumnType.UINT32)]
    relation = f.RelationSchemaBuilder()
    relation.initialize(repository, repository.insert(f.SchemaData([column.type for column in columns]))[0].index(), columns)
    relation.insert(repository, records, [3])
    row = relation.view(repository).row(0)
    assert not hasattr(row, "get_objects")
    assert not hasattr(row, "resolve_object")
    assert not hasattr(row, "resolve_objects")


def test_a_schema_builder_is_set_from_its_records_form_without_a_repository() -> None:
    repository = f.RelationRepository()
    schema = f.SchemaBuilder()
    schema.set_column_types([f.ColumnType.UINT32, f.ColumnType.BOOL])
    view = schema.view(repository)
    assert isinstance(view, f.SchemaBuilderBorrowed)
    assert list(view.get_column_types()) == [f.ColumnType.UINT32, f.ColumnType.BOOL]


def test_a_relation_builder_keeps_its_repository_and_records_alive() -> None:
    repository = f.RelationRepository()
    records = f.RowSchemaRecords()
    columns = [f.ColumnLayoutData(f.ColumnIndex(0), f.ColumnType.UINT32)]
    relation = f.RelationSchemaBuilder()
    relation.initialize(repository, repository.insert(f.SchemaData([f.ColumnType.UINT32]))[0].index(), columns)
    relation.insert(repository, records, [3])
    del records
    gc.collect()
    assert relation.view(repository).contains([3])
    del repository
    gc.collect()
    del relation
    gc.collect()


def test_a_builder_is_made_from_its_fields_by_name() -> None:
    repository = f.RelationRepository()
    schema = f.SchemaBuilder(column_types=[f.ColumnType.BOOL])
    assert list(schema.view(repository).get_column_types()) == [f.ColumnType.BOOL]
    assert list(f.SchemaBuilder().view(repository).get_column_types()) == []


def test_a_row_record_is_stored_in_its_pool_and_read_typed() -> None:
    repository = f.RelationRepository()
    pool = repository.insert(f.SchemaData([f.ColumnType.UINT32, f.ColumnType.BOOL]))[0].index()
    flag = True
    values = [f.encode(7, f.ColumnType.UINT32), f.encode(flag, f.ColumnType.BOOL)]
    row, created = repository.insert(f.RowSchemaData(pool, values))
    assert created
    assert repository.insert(f.RowSchemaData(pool, values)) == (row, False)
    assert list(row.get_values()) == values  # read from its packed slot
    assert row.get(0) == 7
    assert row.get(1) is True
