cmake_minimum_required(VERSION 3.21)
project(InstallArgparse)

include(ExternalProject)

yggdrasil_source(YGGDRASIL_ARGPARSE_GIT_REPOSITORY argparse repository)
yggdrasil_source(YGGDRASIL_ARGPARSE_GIT_TAG argparse revision)

message(STATUS "Dependency \"argparse\"...")

list(APPEND CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
        -DCMAKE_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
        -DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DBUILD_SHARED_LIBS=ON
        -DCMAKE_INSTALL_RPATH=${YGGDRASIL_DEPENDENCY_INSTALL_RPATH}
        -DARGPARSE_BUILD_TESTS=OFF
)

message(STATUS "Preparing external project \"argparse\" with args:")
foreach(CMAKE_ARG ${CMAKE_ARGS})
    message(STATUS "-- ${CMAKE_ARG}")
endforeach()
ExternalProject_Add(
    argparse
        GIT_REPOSITORY ${YGGDRASIL_ARGPARSE_GIT_REPOSITORY}
        GIT_TAG ${YGGDRASIL_ARGPARSE_GIT_TAG}
        PREFIX ${CMAKE_BINARY_DIR}/argparse
        STAMP_DIR ${CMAKE_BINARY_DIR}/argparse/stamp/${YGGDRASIL_ARGPARSE_GIT_TAG}
        STEP_TARGETS download
        UPDATE_COMMAND ""
        CMAKE_ARGS ${CMAKE_ARGS}
)
