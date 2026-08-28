cmake_minimum_required(VERSION 3.21)
project(InstallFMT)

include(ExternalProject)

yggdrasil_source(YGGDRASIL_FMT_GIT_REPOSITORY fmt repository)
yggdrasil_source(YGGDRASIL_FMT_GIT_TAG fmt revision)

message(STATUS "Dependency \"fmt\"...")

list(APPEND CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_INSTALL_PREFIX}
        -DCMAKE_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
        -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON
        -DBUILD_SHARED_LIBS:BOOL=ON
        -DCMAKE_INSTALL_RPATH=${YGGDRASIL_DEPENDENCY_INSTALL_RPATH}
        -DFMT_TEST:BOOL=OFF
)
message(STATUS "Preparing external project \"fmt\" with args:")
foreach (CMAKE_ARG ${CMAKE_ARGS})
    message(STATUS "-- ${CMAKE_ARG}")
endforeach ()

ExternalProject_Add(
        fmt
        GIT_REPOSITORY ${YGGDRASIL_FMT_GIT_REPOSITORY}
        GIT_TAG ${YGGDRASIL_FMT_GIT_TAG}
        PREFIX ${CMAKE_BINARY_DIR}/fmt
        STAMP_DIR ${CMAKE_BINARY_DIR}/fmt/stamp/${YGGDRASIL_FMT_GIT_TAG}
        STEP_TARGETS download
        UPDATE_COMMAND ""
        CMAKE_ARGS ${CMAKE_ARGS}
)
