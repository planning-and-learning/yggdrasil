cmake_minimum_required(VERSION 3.21)

project(yggdrasil_native_sources)

include(GNUInstallDirs)
include(ProcessorCount)

# Build configuration

if(NOT DEFINED YGGDRASIL_JOBS)
    ProcessorCount(YGGDRASIL_JOBS)
endif()
if(NOT YGGDRASIL_JOBS)
    set(YGGDRASIL_JOBS 1)
endif()

set(DEFAULT_BUILD_TYPE "Release")
if(NOT CMAKE_BUILD_TYPE)
    set(
        CMAKE_BUILD_TYPE
        ${DEFAULT_BUILD_TYPE}
        CACHE STRING
        "Choose the type of build, options are: Debug Release"
        FORCE
    )
endif()

set(BUILD_TYPE ${CMAKE_BUILD_TYPE})

message(STATUS "Build shared dependencies: ON")

if(APPLE)
    set(YGGDRASIL_DEPENDENCY_INSTALL_RPATH "@loader_path")
else()
    set(YGGDRASIL_DEPENDENCY_INSTALL_RPATH "$ORIGIN")
endif()
set(
    CMAKE_INSTALL_RPATH
    "${YGGDRASIL_DEPENDENCY_INSTALL_RPATH}"
    CACHE STRING
    "Install runtime path for bundled shared dependencies."
)
set(
    CMAKE_BUILD_WITH_INSTALL_RPATH
    OFF
    CACHE BOOL
    "Use the build-tree runtime path while building bundled dependencies."
)
set(
    CMAKE_INSTALL_RPATH_USE_LINK_PATH
    OFF
    CACHE BOOL
    "Do not append absolute linked library directories to bundled dependency install runtime paths."
)
message(STATUS "Dependency install rpath: ${YGGDRASIL_DEPENDENCY_INSTALL_RPATH}")

foreach(dependency IN ITEMS
    argparse benchmark mpich boost cista fmt
    googletest gtl nanobind nauty oneTBB
)
    add_subdirectory("${dependency}")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${dependency}/licenses/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/${dependency}"
    )
endforeach()
