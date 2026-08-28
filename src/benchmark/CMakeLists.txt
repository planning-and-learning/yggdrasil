cmake_minimum_required(VERSION 3.21)
project(InstallBenchmark)

include(ExternalProject)

yggdrasil_source(YGGDRASIL_BENCHMARK_GIT_REPOSITORY benchmark repository)
yggdrasil_source(YGGDRASIL_BENCHMARK_GIT_TAG benchmark revision)

message(STATUS "Dependency \"benchmark\"...")

find_package(benchmark QUIET PATHS ${CMAKE_INSTALL_PREFIX} NO_DEFAULT_PATH)

if (NOT benchmark_FOUND)

    list(APPEND CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_INSTALL_PREFIX}
            -DCMAKE_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
            -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
            -DBUILD_SHARED_LIBS:BOOL=ON
            -DCMAKE_INSTALL_RPATH=${YGGDRASIL_DEPENDENCY_INSTALL_RPATH}
            -DBENCHMARK_ENABLE_TESTING=OFF
            -DBENCHMARK_ENABLE_GTEST_TESTS=OFF
    )

    message(STATUS "Preparing external project \"benchmark\" with args:")
    foreach(CMAKE_ARG ${CMAKE_ARGS})
        message(STATUS "-- ${CMAKE_ARG}")
    endforeach()

    ExternalProject_Add(
            benchmark
            GIT_REPOSITORY ${YGGDRASIL_BENCHMARK_GIT_REPOSITORY}
            GIT_TAG ${YGGDRASIL_BENCHMARK_GIT_TAG}
            PREFIX ${CMAKE_BINARY_DIR}/benchmark
            STAMP_DIR ${CMAKE_BINARY_DIR}/benchmark/stamp/${YGGDRASIL_BENCHMARK_GIT_TAG}
            STEP_TARGETS download
            UPDATE_COMMAND ""
            CMAKE_ARGS ${CMAKE_ARGS}
    )


else()
    message(STATUS "benchmark is already installed.")
endif()
