cmake_minimum_required(VERSION 3.21)
project(InstallGTL)

include(ExternalProject)
find_package(Git REQUIRED)

yggdrasil_source(YGGDRASIL_GTL_GIT_REPOSITORY gtl repository)
yggdrasil_source(YGGDRASIL_GTL_GIT_TAG gtl revision)

message(STATUS "Dependency \"gtl\"...")

list(APPEND CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
        -DCMAKE_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
        -DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DBUILD_SHARED_LIBS=ON
        -DCMAKE_INSTALL_RPATH=${YGGDRASIL_DEPENDENCY_INSTALL_RPATH}
        # -DCMAKE_CXX_FLAGS="-fsanitize=address"
        # Disable extras that drag in the failing example
        -DGTL_BUILD_EXAMPLES=OFF
        -DGTL_BUILD_TESTS=OFF
        -DGTL_BUILD_BENCHMARKS=OFF
)

message(STATUS "Preparing external project \"gtl\" with args:")
foreach(CMAKE_ARG ${CMAKE_ARGS})
    message(STATUS "-- ${CMAKE_ARG}")
endforeach()
ExternalProject_Add(
    gtl
    GIT_REPOSITORY ${YGGDRASIL_GTL_GIT_REPOSITORY}
    GIT_TAG ${YGGDRASIL_GTL_GIT_TAG}
    PREFIX ${CMAKE_BINARY_DIR}/gtl
    STAMP_DIR ${CMAKE_BINARY_DIR}/gtl/stamp/${YGGDRASIL_GTL_GIT_TAG}
    STEP_TARGETS download
    UPDATE_COMMAND ""
    PATCH_COMMAND
        ${GIT_EXECUTABLE} -C <SOURCE_DIR> apply
        ${CMAKE_CURRENT_LIST_DIR}/clear-empty-node.patch
    CMAKE_ARGS ${CMAKE_ARGS}
)
ExternalProject_Add_Step(
    gtl install-cmake-package
    COMMAND ${CMAKE_COMMAND}
            -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
            -DCMAKE_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
            -S ${CMAKE_CURRENT_LIST_DIR}/package
            -B ${CMAKE_BINARY_DIR}/gtl-package
    COMMAND ${CMAKE_COMMAND}
            --install ${CMAKE_BINARY_DIR}/gtl-package
    DEPENDEES install
)
