cmake_minimum_required(VERSION 3.21)
project(InstallCista)

include(ExternalProject)

yggdrasil_source(YGGDRASIL_CISTA_GIT_REPOSITORY cista repository)
yggdrasil_source(YGGDRASIL_CISTA_GIT_TAG cista revision)

message(STATUS "Dependency \"cista\"...")

# Attempt to find the installed cista library
find_package(cista QUIET PATHS ${CMAKE_INSTALL_PREFIX} NO_DEFAULT_PATH)

if (NOT cista_FOUND)


    list(APPEND CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
            -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
            -DCMAKE_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
            -DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DBUILD_SHARED_LIBS=ON
            -DCMAKE_INSTALL_RPATH=${YGGDRASIL_DEPENDENCY_INSTALL_RPATH}
            -DCISTA_INSTALL=ON
    )

    message(STATUS "Preparing external project \"cista\" with args:")
    foreach(CMAKE_ARG ${CMAKE_ARGS})
        message(STATUS "-- ${CMAKE_ARG}")
    endforeach()
    ExternalProject_Add(
        cista
        GIT_REPOSITORY ${YGGDRASIL_CISTA_GIT_REPOSITORY}
        GIT_TAG ${YGGDRASIL_CISTA_GIT_TAG}
        PREFIX ${CMAKE_BINARY_DIR}/cista
        STAMP_DIR ${CMAKE_BINARY_DIR}/cista/stamp/${YGGDRASIL_CISTA_GIT_TAG}
        STEP_TARGETS download
        UPDATE_COMMAND ""
        CMAKE_ARGS ${CMAKE_ARGS}
    )
else()
    message(STATUS "cista is already installed.")
endif()
