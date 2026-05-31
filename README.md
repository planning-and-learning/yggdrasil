# Yggdrasil

`pyyggdrasil` packages the native dependency prefix used by the planning projects
in this repository family.

The Python distribution name and import package are both `pyyggdrasil`.

## Python Integration

Install the wheel and query the native prefix:

```python
import pyyggdrasil

print(pyyggdrasil.native_prefix())
```

Python packages that consume this native prefix should depend on:

```toml
dependencies = [
    "pyyggdrasil>=0.0.10",
]
```

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

Downstream CMake projects can use the installed native prefix through
`CMAKE_PREFIX_PATH`:

```bash
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="$(python -c 'import pyyggdrasil; print(pyyggdrasil.native_prefix())')"
```
