# Release cycle

The four libraries are released to PyPI as one chain, upstream first:

| Order | Repository | PyPI package | Depends on |
| --- | --- | --- | --- |
| 1 | yggdrasil | `pyyggdrasil` | |
| 2 | loki | `pypddl` | `pyyggdrasil` |
| 3 | tyr | `pytyr` | `pyyggdrasil`, `pypddl` |
| 4 | runir | `pyrunir` | `pyyggdrasil`, `pypddl`, `pytyr` |

Downstream wheels compile against the headers and link against the shared libraries of
the upstream wheels they were built with. A release must therefore keep the API and ABI
that its downstream pins promise, or change its version so that those pins exclude it.

## Versions

The version is set only in `[project].version` of `pyproject.toml`; CMake reads it from
there, and the release workflow fails if the tag does not match it.

Downstream packages pin each upstream twice in `pyproject.toml`, in
`[build-system].requires` and in `[project].dependencies`, as `>=X.Y.Z,<X.(Y+1)`. Within
one minor series every release must stay API and ABI compatible with what downstream
code compiled against: no changed or removed declarations, signatures, class layouts,
template definitions or inline functions that downstream code uses.

Choose the lowest version that is free on PyPI and does not break downstream:

- Compatible changes only: bump the patch version (`0.4.1` to `0.4.2`).
- Any API or ABI break for downstream, or when in doubt: bump the minor version and
  reset the patch (`0.4.1` to `0.5.0`, `1.4.1` to `1.5.0`). The `<X.(Y+1)` caps of the
  existing downstream releases then keep them on the old series.
- A version that was ever uploaded is taken, even if it was yanked or deleted; check
  `https://pypi.org/pypi/<package>/json` (or `pip index versions <package>`) and skip
  taken versions.

A repository needs a release when it changed since its last tag, or when an upstream
release moved to a new minor series: the downstream pins must follow, so the downstream
package changes too. After a minor bump upstream, every package below it in the chain is
released.

## Downstream pins

When an upstream package gets a new version, every downstream `pyproject.toml` that uses
it is updated in both places to `>=<new version>,<<next minor>>`, for example
`"pyyggdrasil>=0.5.0,<0.6"`, also when the new upstream version is only a patch: the
downstream release is built and tested against it and must not resolve to an older one.

The same versions appear outside `pyproject.toml`, and they move with it:

- the `*_REQUIRED_VERSION` variables in the top-level `CMakeLists.txt` (for example
  `TYR_LOKI_REQUIRED_VERSION`), which `find_package` checks: after a minor bump it
  rejects the new upstream until they are raised;
- the packages that the `peachycheck` workflow's `prepare` step installs;
- install commands in the READMEs and in `docs/`.

## Apptainer images

loki and tyr also build an Apptainer image (`Apptainer` recipe, `apptainer` workflow) on
every push to `main`. The recipe installs the dependencies of `pyproject.toml` from PyPI,
so it uses the same pins and builds only once the pinned upstream versions are
published. Versions written in the recipe or the workflow (base image, Python,
`apptainer-version`) are checked and updated with the release when needed. The image is
pushed as `oras://ghcr.io/<repository>:latest` only; a release has no image of its own
unless the workflow also pushes `:v<version>`.

## Steps per release

Release the repositories in the order of the table; start a repository only after the
previous one is published on PyPI, because its release builds resolve the upstream
packages from PyPI.

1. Collect the changes since the last tag (`git log v<last>..HEAD`) and choose the version
   as above.
2. Set `[project].version`, and update the pins on the upstream packages released in this
   cycle.
3. Bring the release state to `main` (from `dev`) and push it, without the tag.
4. Wait until every workflow on that commit (`ci_ctest`, `ci_pytest`, `ci_release`, and
   `apptainer` or `ci_tsan` where present) has succeeded, and check their logs: no errors
   and no compiler or packaging warnings. Fix and repeat from step 3 otherwise. A
   `testpypi` run with a staging version can check the upload and installation first.
5. Only then create the tag `v<version>` on that commit of `main` and push it. The
   `release` workflow builds the wheels and the source distribution, checks the version
   against the tag, creates the GitHub release and publishes to PyPI.
6. Wait until the `release` workflow has succeeded, also without warnings, and the
   version is listed on PyPI and installs (`pip install <package>==<version>`), before
   the next repository in the chain.

A pushed tag is never moved or deleted, and a published version is never reused. If a
release turns out broken, fix it in a new version.
