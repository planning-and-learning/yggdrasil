# Yggdrasil

`pyyggdrasil` packages the native dependency prefix used by the planning projects
in this repository family.

The Python distribution name and import package are both `pyyggdrasil`; Python 3.11 or newer is required.

`pyyggdrasil` is the base of the planning-and-learning package chain. The shared
workspace layout, the layered install order, and the common
build-from-source and CMake-integration patterns are documented in the
[Planning and Learning build instructions](https://github.com/planning-and-learning/.github/blob/main/profile/README.md#building-from-source);
the sections below cover `pyyggdrasil`-specific details.

Each binary release has a matching complete corresponding-source archive on the
[GitHub releases page](https://github.com/planning-and-learning/yggdrasil/releases),
containing Yggdrasil and its exact native dependency sources.

## Python Integration

Install the wheel and query the native prefix:

```python
import pyyggdrasil

print(pyyggdrasil.native_prefix())
print(pyyggdrasil.cmake_prefix())  # prefix to put on CMAKE_PREFIX_PATH
print(pyyggdrasil.cmake_dir())     # directory containing yggdrasilConfig.cmake
print(pyyggdrasil.include_dir())
print(pyyggdrasil.library_dirs())
print(pyyggdrasil.cmake_dirs())
```

The same paths are available from the shell:

```bash
python -m pyyggdrasil --prefix
python -m pyyggdrasil --cmake-dir
python -m pyyggdrasil --include-dir
python -m pyyggdrasil --version
```

Python packages that consume this native prefix should depend on:

```toml
dependencies = [
    "pyyggdrasil>=0.1,<0.2",
]
```

The bundled shared libraries make the coupling ABI-level, so pin to the same
minor version. The exported CMake package version file uses
`SameMinorVersion` compatibility accordingly.

## Build Python

Build a wheel from source:

```bash
uv build --wheel
```

The build recreates `dependencies-build/` and `dependencies-install/` from
scratch. To package an existing native prefix without rebuilding dependencies:

```bash
YGGDRASIL_BUILD_NATIVE=OFF \
YGGDRASIL_NATIVE_PREFIX=/path/to/dependencies-install \
uv build --wheel
```

This bypass trusts the prefix as-is; use only one built from the dependency
revisions in `docs/SOURCES.json`.

Native builds use all available processors by default. Set `YGGDRASIL_JOBS`
to override the build parallelism.
Set `YGGDRASIL_BUILD_TYPE` to select the dependency build profile; it defaults
to `Release`.

Runtime libraries are stripped in the wheel by default. Disable that for
debugging with:

```bash
YGGDRASIL_STRIP_WHEEL=OFF uv build --wheel
```

## Build C++

### Native Dependencies

Build the native dependency prefix directly with CMake:

```bash
cmake -S src -B dependencies-build \
  -DCMAKE_INSTALL_PREFIX=dependencies-install \
  -DCMAKE_INSTALL_LIBDIR=lib

cmake --build dependencies-build --parallel
cmake --install dependencies-build
```

Yggdrasil builds its bundled dependencies as shared libraries. The native
dependency prefix contains C++ headers, shared libraries, and CMake package
configuration files consumed by the other projects.

### Yggdrasil Targets

Configure Yggdrasil separately against the installed dependency prefix:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DYGGDRASIL_NATIVE_PREFIX="$PWD/dependencies-install" \
  -DYGGDRASIL_BUILD_TESTS=ON

cmake --build build --parallel
ctest --test-dir build
```

CMake options:

| Option | Default | Description |
| --- | --- | --- |
| `YGGDRASIL_BUILD_TESTS` | `OFF` | Build Yggdrasil C++ tests. |
| `YGGDRASIL_BUILD_PROFILING` | `OFF` | Build Yggdrasil benchmark executables. |
| `YGGDRASIL_ENABLE_FMT_FORMATTERS` | `ON` | Enable Yggdrasil's public fmt formatters. |
| `YGGDRASIL_USE_LLD` | `ON` | Use LLVM `lld` with Clang when available. |
| `YGGDRASIL_ENABLE_LTO` | `ON` | Enable link-time optimization for Release builds. |

Single-config CMake builds default to Release. On GCC and Clang, Debug builds
use `-Og` with debug symbols, RelWithDebInfo keeps frame pointers and disables
LTO, and Release LTO uses GCC LTO or Clang ThinLTO. Editable installs and
wheels disable `YGGDRASIL_USE_LLD` and `YGGDRASIL_ENABLE_LTO` by default for
build reliability. This compiler policy applies only to Yggdrasil's
first-party extension and tests; the `src/` dependency superbuild leaves each
bundled project in control of its own compiler policy.

## CMake Integration

The wheel ships a CMake package config (`yggdrasilConfig.cmake`) defining the
`yggdrasil::yggdrasil` interface target. It carries the include directory,
C++20 as a compile feature, and the bundled dependencies (Boost, fmt, gtl, TBB)
via `find_dependency`. (cista is consumed through the include directory only —
its target exports compile definitions that would otherwise change cista's
hashing and fmt integration.) Downstream CMake projects consume it through
`CMAKE_PREFIX_PATH`:

```bash
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="$(python -m pyyggdrasil --prefix)"
```

```cmake
find_package(yggdrasil 0.1 CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE yggdrasil::yggdrasil)
```

For compiler invocations launched from Python, use `pyyggdrasil.include_dir()`
for the C++ headers and `pyyggdrasil.library_dirs()` for installed native
library directories.

### MPI

The wheel includes a TCP/shared-memory MPICH runtime and Boost.MPI. MPI remains
opt-in and is not linked by `yggdrasil::yggdrasil`:

```cmake
find_package(yggdrasil CONFIG REQUIRED)
set(MPI_HOME "${YGGDRASIL_NATIVE_PREFIX}")
set(MPI_CXX_SKIP_MPICXX ON)
find_package(MPI REQUIRED COMPONENTS CXX)
find_package(Boost CONFIG REQUIRED COMPONENTS mpi serialization
    PATHS "${YGGDRASIL_NATIVE_PREFIX}" NO_DEFAULT_PATH)
target_link_libraries(distributed PRIVATE
    MPI::MPI_CXX Boost::mpi Boost::serialization)
```

The launcher is `${YGGDRASIL_NATIVE_PREFIX}/bin/mpiexec`, or from Python,
`pyyggdrasil.native_prefix() / "bin" / "mpiexec"`. Use the bundled launcher
and libraries together; vendor interconnects and vendor MPI require a separate
source build.

## fmt Formatters

Yggdrasil's public `fmt::formatter` specializations are guarded by the
`YGG_ENABLE_FMT_FORMATTERS` macro, which defaults to `1`
(`yggdrasil/core/config.hpp`). Consumers can opt out by defining
`YGG_ENABLE_FMT_FORMATTERS=0`. For Yggdrasil's own builds, the
`YGGDRASIL_ENABLE_FMT_FORMATTERS` CMake option (default `ON`) toggles the
macro.
