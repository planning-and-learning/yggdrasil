cmake_minimum_required(VERSION 3.21)
project(InstallNanobind)

include(ExternalProject)

set(YGGDRASIL_NANOBIND_GIT_TAG v2.12.0 CACHE STRING "Git tag used to build nanobind.")

message(STATUS "Dependency \"nanobind\"...")

find_package(Python 3.9
  REQUIRED COMPONENTS Interpreter Development.Module
  OPTIONAL_COMPONENTS Development.SABIModule)

# The nanobind external project installs the upstream datadir layout
# (<prefix>/nanobind/{include,src,ext,cmake}), which contains the runtime
# sources. The shared runtime project builds libnanobind from it, installs the
# flattened layout used by all other bundled dependencies
# (<prefix>/{include,lib,lib/cmake/nanobind,bin/stubgen.py}), and removes the
# datadir afterwards: with a prebuilt shared runtime, consumers never compile
# nanobind from source.
set(YGGDRASIL_NANOBIND_DATADIR_CONFIG
    "${CMAKE_INSTALL_PREFIX}/nanobind/cmake/nanobind-config.cmake")
set(YGGDRASIL_NANOBIND_FLATTENED_CONFIG
    "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/cmake/nanobind/nanobind-config.cmake")
set(YGGDRASIL_NANOBIND_SHARED_RUNTIME_LIBRARY
    "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/${CMAKE_SHARED_LIBRARY_PREFIX}nanobind${CMAKE_SHARED_LIBRARY_SUFFIX}")

if(EXISTS "${YGGDRASIL_NANOBIND_FLATTENED_CONFIG}" AND EXISTS "${YGGDRASIL_NANOBIND_SHARED_RUNTIME_LIBRARY}")
    set(YGGDRASIL_NANOBIND_SHARED_RUNTIME_INSTALLED TRUE)
else()
    set(YGGDRASIL_NANOBIND_SHARED_RUNTIME_INSTALLED FALSE)
endif()

set(YGGDRASIL_NANOBIND_SHARED_RUNTIME_DEPENDS)

if(NOT YGGDRASIL_NANOBIND_SHARED_RUNTIME_INSTALLED AND NOT EXISTS "${YGGDRASIL_NANOBIND_DATADIR_CONFIG}")
    list(APPEND CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
            -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_INSTALL_PREFIX}
            -DCMAKE_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
            -DNB_TEST=OFF
    )

    message(STATUS "Preparing external project \"nanobind\" with args:")
    foreach(CMAKE_ARG ${CMAKE_ARGS})
        message(STATUS "-- ${CMAKE_ARG}")
    endforeach()

    ExternalProject_Add(
            nanobind
            GIT_REPOSITORY https://github.com/wjakob/nanobind.git
            GIT_TAG ${YGGDRASIL_NANOBIND_GIT_TAG}
            PREFIX ${CMAKE_BINARY_DIR}/nanobind
            STAMP_DIR ${CMAKE_BINARY_DIR}/nanobind/stamp/${YGGDRASIL_NANOBIND_GIT_TAG}
            UPDATE_COMMAND ""
            CMAKE_ARGS ${CMAKE_ARGS}
    )
    list(APPEND YGGDRASIL_NANOBIND_SHARED_RUNTIME_DEPENDS nanobind)
endif()

if(NOT YGGDRASIL_NANOBIND_SHARED_RUNTIME_INSTALLED)
    ExternalProject_Add(
            nanobind_shared_runtime
            SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shared_runtime"
            PREFIX ${CMAKE_BINARY_DIR}/nanobind-shared-runtime
            STAMP_DIR ${CMAKE_BINARY_DIR}/nanobind/stamp/${YGGDRASIL_NANOBIND_GIT_TAG}-shared-runtime
            UPDATE_COMMAND ""
            CMAKE_ARGS
                -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
                -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_INSTALL_PREFIX}
                -DCMAKE_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
                -DPython_EXECUTABLE=${Python_EXECUTABLE}
            DEPENDS ${YGGDRASIL_NANOBIND_SHARED_RUNTIME_DEPENDS}
    )
else()
    message(STATUS "nanobind shared runtime is already installed.")
endif()
