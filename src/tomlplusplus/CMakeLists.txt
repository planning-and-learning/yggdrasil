cmake_minimum_required(VERSION 3.21)
project(InstallTomlplusplus)

include(ExternalProject)

yggdrasil_source(YGGDRASIL_TOMLPLUSPLUS_GIT_REPOSITORY tomlplusplus repository)
yggdrasil_source(YGGDRASIL_TOMLPLUSPLUS_GIT_TAG tomlplusplus revision)

ExternalProject_Add(
    tomlplusplus
    GIT_REPOSITORY ${YGGDRASIL_TOMLPLUSPLUS_GIT_REPOSITORY}
    GIT_TAG ${YGGDRASIL_TOMLPLUSPLUS_GIT_TAG}
    PREFIX ${CMAKE_BINARY_DIR}/tomlplusplus
    STAMP_DIR ${CMAKE_BINARY_DIR}/tomlplusplus/stamp/${YGGDRASIL_TOMLPLUSPLUS_GIT_TAG}
    STEP_TARGETS download
    UPDATE_COMMAND ""
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_INSTALL_PREFIX}
        -DCMAKE_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
        -DBUILD_EXAMPLES=OFF
)
