# Yggdrasil

`pyyggdrasil` packages the native dependency prefix used by the planning projects
in this repository family.

The Python distribution name and import package are both `pyyggdrasil`.

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
    "pyyggdrasil>=0.0.17,<0.1",
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

The build creates `dependencies-build/` and `dependencies-install/`. To package
an existing native prefix without rebuilding dependencies:

```bash
YGGDRASIL_BUILD_NATIVE=OFF \
YGGDRASIL_NATIVE_PREFIX=/path/to/dependencies-install \
uv build --wheel
```

Set `YGGDRASIL_JOBS` to control the native dependency build parallelism.

Runtime libraries are stripped in the wheel by default. Disable that for
debugging with:

```bash
YGGDRASIL_STRIP_WHEEL=OFF uv build --wheel
```

## Build C++

Build the native dependency prefix directly with CMake:

```bash
cmake -S src -B dependencies-build \
  -DCMAKE_INSTALL_PREFIX=dependencies-install \
  -DCMAKE_INSTALL_LIBDIR=lib

cmake --build dependencies-build -j4
cmake --install dependencies-build
```

Yggdrasil builds its bundled dependencies as shared libraries. The native
dependency prefix contains C++ headers, shared libraries, and CMake package
configuration files consumed by the other projects.

## CMake Integration

The wheel ships a CMake package config (`yggdrasilConfig.cmake`) defining the
`yggdrasil::yggdrasil` interface target. It carries the include directory,
C++20 as a compile feature, and the bundled dependencies (Boost, cista, fmt,
gtl, TBB) via `find_dependency`. Downstream CMake projects consume it through
`CMAKE_PREFIX_PATH`:

```bash
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="$(python -m pyyggdrasil --prefix)"
```

```cmake
find_package(yggdrasil 0.0.17 CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE yggdrasil::yggdrasil)
```

For compiler invocations launched from Python, use `pyyggdrasil.include_dir()`
for the C++ headers and `pyyggdrasil.library_dirs()` for installed native
library directories.

## fmt Formatters

Yggdrasil's public `fmt::formatter` specializations are guarded by the
`YGG_ENABLE_FMT_FORMATTERS` macro, which defaults to `1`
(`yggdrasil/core/config.hpp`). Consumers can opt out by defining
`YGG_ENABLE_FMT_FORMATTERS=0`. For Yggdrasil's own builds, the
`YGGDRASIL_ENABLE_FMT_FORMATTERS` CMake option (default `ON`) toggles the
macro. The sibling libraries follow the same convention with
`LOKI_`/`TYR_`/`RUNIR_ENABLE_FMT_FORMATTERS`, and each library's CMake option
also toggles the macros of its upstream libraries.
