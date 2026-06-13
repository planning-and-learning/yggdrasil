include("${CMAKE_CURRENT_LIST_DIR}/test_prelude.cmake")

set(test_prefix "${YGGDRASIL_TEST_BINARY_DIR}/patch_python_stubs/prefix")
set(test_destdir "${YGGDRASIL_TEST_BINARY_DIR}/patch_python_stubs/destdir")
set(stub_root "${test_destdir}${test_prefix}/pyyggdrasil")
set(stub_file "${stub_root}/api.pyi")
set(private_stub_root "${stub_root}/_pyyggdrasil")
set(private_init_stub "${private_stub_root}/__init__.pyi")
set(private_core_stub "${private_stub_root}/core.pyi")
set(private_flat_stub "${private_stub_root}/flat.pyi")
set(private_execution_stub "${private_stub_root}/execution.pyi")
set(public_init_stub "${stub_root}/__init__.pyi")
set(public_core_stub "${stub_root}/core.pyi")
set(public_core_init_stub "${stub_root}/core/__init__.pyi")
set(public_flat_stub "${stub_root}/flat.pyi")
set(public_execution_init_stub "${stub_root}/execution/__init__.pyi")

file(REMOVE_RECURSE "${test_destdir}${test_prefix}")

file(MAKE_DIRECTORY "${stub_root}" "${private_stub_root}" "${stub_root}/core" "${stub_root}/execution")
file(WRITE "${stub_file}" [=[value: pyyggdrasil._pyyggdrasil.execution.ExecutionContext
]=])
file(WRITE "${private_init_stub}" [=[private root: pyyggdrasil._pyyggdrasil.execution.ExecutionContext
]=])
file(WRITE "${public_init_stub}" [=[public root: pyyggdrasil._pyyggdrasil.execution.ExecutionContext
]=])
file(WRITE "${private_core_stub}" [=[value: pyyggdrasil._pyyggdrasil.execution.ExecutionContext
]=])
file(WRITE "${private_flat_stub}" [=[private flat: pyyggdrasil._pyyggdrasil.execution.ExecutionContext
]=])
file(WRITE "${private_execution_stub}" [=[private execution: pyyggdrasil._pyyggdrasil.execution.ExecutionContext
]=])
file(WRITE "${public_flat_stub}" [=[public flat: pyyggdrasil._pyyggdrasil.execution.ExecutionContext
]=])
file(WRITE "${public_execution_init_stub}" [=[public execution: pyyggdrasil._pyyggdrasil.execution.ExecutionContext
]=])

set(CMAKE_INSTALL_PREFIX "${test_prefix}")
set(ENV{DESTDIR} "${test_destdir}")
include("${YGGDRASIL_PROJECT_ROOT}/cmake/yggdrasilPatchPythonStubs.cmake")
yggdrasil_patch_python_stubs(PACKAGE pyyggdrasil PRIVATE_MODULE _pyyggdrasil RENAME_PACKAGES pyyggdrasil)

file(READ "${stub_file}" patched_stub)
if(NOT patched_stub STREQUAL [=[value: pyyggdrasil.execution.ExecutionContext
]=])
    message(FATAL_ERROR "Stub patch did not honor DESTDIR. Got: ${patched_stub}")
endif()

if(EXISTS "${private_stub_root}")
    message(FATAL_ERROR "Private stub package was not removed: ${private_stub_root}")
endif()

file(READ "${public_init_stub}" patched_init_stub)
if(NOT patched_init_stub STREQUAL [=[public root: pyyggdrasil.execution.ExecutionContext
]=])
    message(FATAL_ERROR "Existing public __init__.pyi should be patched but not overwritten. Got: ${patched_init_stub}")
endif()

if(EXISTS "${public_core_stub}")
    message(FATAL_ERROR "Private core.pyi should be installed as core/__init__.pyi when core is a package.")
endif()

if(NOT EXISTS "${public_core_init_stub}")
    message(FATAL_ERROR "Private core.pyi was not moved to the public package __init__.pyi.")
endif()

if(NOT EXISTS "${public_flat_stub}")
    message(FATAL_ERROR "Private flat.pyi was not moved to the public package.")
endif()

if(EXISTS "${stub_root}/execution.pyi")
    message(FATAL_ERROR "Private execution.pyi should be installed as execution/__init__.pyi when execution is a package.")
endif()

file(READ "${public_core_init_stub}" patched_core_stub)
if(NOT patched_core_stub STREQUAL [=[value: pyyggdrasil.execution.ExecutionContext
]=])
    message(FATAL_ERROR "Moved private package stub was not patched. Got: ${patched_core_stub}")
endif()

file(READ "${public_flat_stub}" patched_flat_stub)
if(NOT patched_flat_stub STREQUAL [=[public flat: pyyggdrasil.execution.ExecutionContext
]=])
    message(FATAL_ERROR "Existing public flat stub should be patched but not overwritten. Got: ${patched_flat_stub}")
endif()

file(READ "${public_execution_init_stub}" patched_execution_stub)
if(NOT patched_execution_stub STREQUAL [=[public execution: pyyggdrasil.execution.ExecutionContext
]=])
    message(FATAL_ERROR "Existing public execution stub should be patched but not overwritten. Got: ${patched_execution_stub}")
endif()
