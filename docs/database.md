# Relational algebra

`<yggdrasil/database/operations.hpp>` provides header-only set operations in
`ygg::database`. Relations have a runtime schema and packed, fixed-width rows.
Different columns can hold different types without a tag or variant in every cell.
Nullary relations and runtime arities remain supported.

## Types and schemas

The template parameter is a `ygg::TypeList` of supported value types, not one
cell type. `DefaultColumnTypes` contains `uint32_t`, `int32_t`, `uint64_t`,
`int64_t`, `float`, `double`, and `bool`. A smaller or extended list can be used
consistently for relations, schemas, plans, workspaces, and repositories.

```cpp
#include <yggdrasil/database/operations.hpp>

using namespace ygg::database;
using Values = ygg::TypeList<std::uint32_t, double>;
const ygg::Index<Column> source(0), target(1), distance(2);

ygg::Builder<Columns<Values>> columns;
columns.push_back<std::uint32_t>(source);
columns.push_back<std::uint32_t>(target);
columns.push_back<double>(distance);
ygg::Builder<Relation<Values>> paths(std::move(columns));
paths.insert(std::tuple { std::uint32_t(3), std::uint32_t(8), 2.5 });
paths.insert(std::tuple { std::uint32_t(3), std::uint32_t(9), 4.0 });

const auto length = paths[0].get<double>(distance);
auto reachable = project<Values>(paths, {source, target});
auto exact = select_equal_value<Values>(paths, distance, 2.5);
```

`Column` identifies a schema-local label. `Columns<Values>` follows the usual
`Builder`, `Data`, `Index`, `View` structure. Each `ColumnLayout` records a label,
type ordinal, byte offset, and width. Identity depends on the ordered labels
and types; offsets and widths are derived and validated. `.span()` borrows the
layout, `.row_size()` returns its packed byte width, and `column_index(label)`
resolves a label. Labels must be unique. Constructing a schema from labels alone
uses the first type in `Values` for every column.

`ColumnCodec<T>` is the extension point for additional fixed-width values.
`ColumnValue` requires a constant positive `size`, `encode(value, byte_span)`
and `decode(const_byte_span)`. Equal values must have identical encodings;
decoding must preserve their semantic value. Explicit codecs prevent padding
or uninitialized object bytes from becoming part of a key. Scalar `Index<T>`
types derived from `IndexMixin` use the existing `IndexCoder`. Composite indices
need an explicit codec.

`ColumnValueFor<T, Values>` additionally requires `T` to be registered in the
column family. Typed access and tuple operations use this shared constraint.

Integer and floating-point codecs copy through `memcpy`; packed fields need no
alignment and never expose typed references into bytes. Floating-point encoding
normalizes signed zero and all NaNs, preserving every other finite value and
infinity exactly. Boolean encoding is one byte, 0 or 1. No tolerance rounding is
performed. This is a native-endian in-process representation, not a portable wire
format. Previously serialized homogeneous relation records are incompatible.

## Rows and ownership

`ygg::Builder<Relation<Values>>` owns a schema and `RawArraySet<std::byte>`.
`row(i)` exposes canonical bytes for prepared operations. `operator[]` and
`at(i)` return a borrowed `Row<Values>`: `get<T>(label)` checks the requested type
and returns a value; `visit(position, visitor)` dispatches a runtime column type.
The visitor must return one common result type. `.bytes()` and `.columns()`
expose the borrowed spans.

A `Row` borrows an already validated schema and canonical bytes. Its construction
has those preconditions, like constructing a span from valid storage. Inserting
such a row checks the schema without decoding every field. Inserting a raw byte
span validates width and canonical field encodings before mutation. Inserting
`std::tuple` values checks arity/types and encodes into a fixed-size stack buffer.
`find` and `contains` offer the same tuple, typed-row, and raw-byte interfaces.

Contextual `ygg::View` over relation `Builder`, `Data`, or `Index` handles uses
`View<Row<Values>, Context>` for indexing and iteration. Primitive columns remain
values; typed indices resolve through `make_view` and the context's repository.
This keeps object-view construction at the external access boundary. Row and
relation iterators borrow storage; they do not copy payloads.

Builders are movable and noncopyable. Insertion retains existing row storage;
erase moves the last row into the erased slot. Clear invalidates rows but retains
capacity. Schema replacement invalidates borrowed schema spans. Keep a borrowed
row's owner and schema alive and unchanged. A returned pool entry cannot be used
through an old view. There is no internal synchronization.

## Operations

All operations accept builders or contextual views satisfying
`RelationViewConcept<V, Values>`. Output/workspace overloads deduce `Values`;
return-by-value overloads take it explicitly.

| Operation | Meaning |
| --- | --- |
| `select<Values>(input, predicate)` | Keep rows accepted by a predicate over `Row<Values>`. |
| `select_equal_columns<Values>(input, a, b)` | Compare columns with the same registered type. |
| `select_equal_value<Values>(input, column, value)` | Match a constant encoded by that column's codec. |
| `project<Values>(input, labels)` | Keep the ordered columns and eliminate duplicate rows. |
| `join<Values>(lhs, rhs)` | Natural join on common labels, followed by rhs-only columns. |
| `union_<Values>(lhs, rhs)` | Union with an identical ordered typed schema. |
| `difference<Values>(lhs, rhs)` | Difference with an identical ordered typed schema. |
| `builder.rename(labels)` | Change labels while retaining types and rows. |

Common join labels and equality columns must have matching types. Projection
preserves field types and repacks offsets. Wrong types, incompatible schemas,
duplicate labels, and wrong row widths throw before clearing the output.
Unknown labels throw `std::out_of_range`. Exceptions during evaluation (for
example, a throwing predicate) may leave partial output. An output cannot alias
an input. `assign(destination, source)` supports self-assignment, replaces the
schema and rows, and retains buffers when byte widths match.

Prepare `JoinPlan<Values>` and `ProjectionPlan<Values>` once per expression.
Their byte slices resolve types, offsets, and widths outside the row loop:

```cpp
const ProjectionPlan<Values> plan(paths.columns().span(), {source, target});
ygg::Builder<Relation<Values>> output(plan.output_columns().span());
Workspace<Values> workspace;
project(paths, plan, output, workspace);
```

Full and incremental operations share byte-key hashing, equality, and field
copying. Row hashing uses the existing byte hashing path. Join keys stream their
selected slices into XXHash; collisions compare actual key bytes. A join indexes
the smaller input with `UnorderedMultiMap`. `JoinIndexCache<Values>` may retain
indexes for immutable inputs. Its key includes ordered field types and offsets,
so equal bytes with different interpretations cannot reuse an incompatible index.
Renaming labels can still reuse a compatible index.

## Reuse and interning

Keep a `Workspace<Values>` and output builders across evaluations. Scratch byte
buffers, row containers, and hash tables retain capacity. `RelationPool<Values>`
uses existing `UniqueObjectPool` instances grouped by **row byte width**, not
logical arity. Prepare a schema once and pass its layout span to checkout to
avoid rebuilding it in the update loop. Larger workloads may still grow storage.
Each evaluator/thread needs its own mutable workspace and pools.

`RelationRepository<Values>` interns schemas and compact relation records in the
existing symbol repository. Individual byte rows and sorted row-ID vectors are
interned in `RawVectorSet`s. Relations with identical typed schemas and row sets
share identity regardless of insertion order. Identical bytes may share physical
rows across schemas, but distinct typed schemas retain distinct relation identity.
Iteration follows sorted interned row IDs, not value order.

```cpp
RelationRepositoryFactory<Values> factory;
auto repository = factory.create();
auto [stored, created] = insert(repository, paths);
paths.clear(); // stored retains its canonical rows
```

`insert(repository, builder)` publishes and synchronizes the builder's index.
`copy(source, repository)` remaps canonical IDs to another repository.
`repository.rename(view, labels)` preserves the source context, field types,
row-ID set, and namespace. A caller may supply a different schema namespace.
Repository clear invalidates all its views while retaining reusable storage.
Reset caches borrowing those rows before clearing. When temporary and canonical
inputs share a join cache, derive their factories from one `RelationPoolFactory`
so storage IDs share one sequence.

A zero-column empty relation represents false. `insert(std::tuple {})` makes it
true. Projection of a nonempty input onto no columns yields true. An empty
object domain needs no special-case representation.

## Incremental evaluation

`database/incremental/projection.hpp` and `database/incremental/join.hpp` retain
current results and maintain added/removed row sets. Deltas use the same typed
schemas and packed rows. Projection tracks witnesses; a change that replaces a
witness without changing membership produces no output delta. Join supports
updates to either or both operands. Prepared byte operations are shared with
full recomputation. Existing reset, apply, undo, and storage ownership rules
remain unchanged.

## Python

```python
from pyyggdrasil import database as db

relation = db.Relation([0, 1, 2],
                       [db.ColumnType.UINT32, db.ColumnType.UINT32,
                        db.ColumnType.FLOAT64])
relation.insert([3, 8, 2.5])
assert tuple(relation[0]) == (3, 8, 2.5)
assert relation.columns().type(2) == db.ColumnType.FLOAT64
```

Labels-only construction defaults to `UINT32`. `ColumnType` also exposes `INT32`,
`UINT64`, `INT64`, `FLOAT32`, and `BOOL`. Rows yield Python values of the column's
type; no packed bytes leak through the Python API. Pool checkout accepts the
same label/type arguments. `.columns()` remains a borrowed label sequence with
`type(position)` for schema inspection. Rows, views, and schemas keep their
Python owners alive; explicitly clearing an owner still invalidates them.

Run the native regression suite with
`ctest --test-dir <build> -R '^database_' --output-on-failure`.
It covers full and incremental operations, interned identities, canonical
encodings, schema validation, and retained allocations. Python lifetime and
mixed-type behavior are covered in `python/tests/test_database.py`.
