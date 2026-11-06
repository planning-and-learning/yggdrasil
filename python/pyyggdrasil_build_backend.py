# Copyright (C) 2026 Dominik Drexler
# SPDX-License-Identifier: GPL-3.0-or-later

"""PEP 517 build backend of pyyggdrasil: pybrokkr's backend, building against pyurd's native prefix (urd and the
native libraries it ships)."""

from pybrokkr.build_backend import ProviderBackend

# CMake publishes pyyggdrasil's stubs (brokkr_patch_python_stubs), so the backend only strips.
BACKEND = ProviderBackend(
    package="pyyggdrasil",
    providers=("pyurd",),
    prefix_defines={"pyurd": "URD_NATIVE_PREFIX"},
    cmake_defines=("-DCMAKE_INSTALL_LIBDIR=lib",),
    jobs_env="YGGDRASIL_JOBS",
    strip_env="YGGDRASIL_STRIP_WHEEL",
    fix_stubs=False,
    strip_unneeded=True,
)
BACKEND.install_hooks(globals())
