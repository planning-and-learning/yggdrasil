cmake_minimum_required(VERSION 3.21)
project(InstallOneTBB)

include(ExternalProject)

yggdrasil_source(YGGDRASIL_ONETBB_GIT_REPOSITORY oneTBB repository)
yggdrasil_source(YGGDRASIL_ONETBB_GIT_TAG oneTBB revision)

message(STATUS "Dependency \"oneTBB\"...")

# Attempt to find the installed oneTBB library
find_package(oneTBB QUIET PATHS ${CMAKE_INSTALL_PREFIX} NO_DEFAULT_PATH)

if (NOT oneTBB_FOUND)
    list(APPEND CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
            -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
            -DCMAKE_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
            -DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DBUILD_SHARED_LIBS=ON
            -DCMAKE_INSTALL_RPATH=${YGGDRASIL_DEPENDENCY_INSTALL_RPATH}
            -DTBB_STRICT=OFF
            -DTBB_TEST=OFF
            -DTBB_DISABLE_HWLOC_AUTOMATIC_SEARCH=ON
    )

    message(STATUS "Preparing external project \"oneTBB\" with args:")
    foreach(CMAKE_ARG ${CMAKE_ARGS})
        message(STATUS "-- ${CMAKE_ARG}")
    endforeach()
    ExternalProject_Add(
        oneTBB
        GIT_REPOSITORY ${YGGDRASIL_ONETBB_GIT_REPOSITORY}
        GIT_TAG ${YGGDRASIL_ONETBB_GIT_TAG}
        PREFIX ${CMAKE_BINARY_DIR}/oneTBB
        STAMP_DIR ${CMAKE_BINARY_DIR}/oneTBB/stamp/${YGGDRASIL_ONETBB_GIT_TAG}
        STEP_TARGETS download
        UPDATE_COMMAND ""
        CMAKE_ARGS ${CMAKE_ARGS}
    )
else()
    message(STATUS "oneTBB is already installed.")
endif()
