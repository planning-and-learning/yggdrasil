# Release cycle

The four libraries are released to PyPI one after another, upstream first:

| Order | Repository | PyPI package | Depends on |
| --- | --- | --- | --- |
| 1 | yggdrasil | `pyyggdrasil` | |
| 2 | loki | `pypddl` | `pyyggdrasil` |
| 3 | tyr | `pytyr` | `pyyggdrasil`, `pypddl` |
| 4 | runir | `pyrunir` | `pyyggdrasil`, `pypddl`, `pytyr` |

Downstream wheels compile against the headers and link against the libraries of the upstream
wheels, so a release must keep the API and ABI its downstream pins promise.

## Version

Take the lowest version that is free on PyPI (a version that was ever uploaded is taken) and
does not break downstream:

- compatible changes only: next patch version (`0.4.1` to `0.4.2`);
- any API or ABI break, or when in doubt: next minor version (`0.4.1` to `0.5.0`,
  `1.4.1` to `1.5.0`).

The version is set in `[project].version` of `pyproject.toml`; CMake reads it from there.

## Pins

Downstream repositories pin every upstream package at the version just released, as
`>=X.Y.Z,<X.(Y+1)`, in:

- `pyproject.toml`: `[build-system].requires`, `[project].dependencies` and the
  `before-build` of `[tool.cibuildwheel]`;
- `CMakeLists.txt`: `<PROJECT>_<UPSTREAM>_REQUIRED_VERSION`, which `find_package` and the
  installed `Config.cmake` use;
- the install commands in `README.md` and `docs/`.

## Steps

For each repository in order:

1. Wait until the upstream versions of this cycle are in PyPI's simple index
   (`https://pypi.org/simple/<package>/`).
2. On `dev-features`, commit the version and the pins as `Release X.Y.Z: <patch|minor>
   version bump` and push.
3. Fast-forward `main` to `dev-features` (`main` must have no other commits) and push it.
4. Wait until every workflow on `main` has succeeded without warnings from our code
   (warnings from building third-party dependencies do not count). Fix on `dev-features` and
   repeat from step 3 otherwise. A job that failed only because an upstream version was not
   yet in the index is rerun.
5. Push the tag `vX.Y.Z` on that commit of `main`. The `release` workflow builds the wheels and
   the source distribution, checks the tag against the version, creates the GitHub release
   and publishes to PyPI.
6. Wait until the `release` workflow has succeeded and `pip install <package>==X.Y.Z` works.

A pushed tag is never moved or deleted, and a published version is never reused; a broken
release is fixed in a new version.
