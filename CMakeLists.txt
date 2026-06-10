cmake_minimum_required(VERSION 3.21)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/read_project_version.cmake")
yggdrasil_read_project_version(YGGDRASIL_PROJECT_VERSION "${CMAKE_CURRENT_SOURCE_DIR}/pyproject.toml")

project(yggdrasil VERSION "${YGGDRASIL_PROJECT_VERSION}")

include(GNUInstallDirs)

set(YGGDRASIL_NATIVE_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/dependencies-install" CACHE PATH "Native dependency prefix to package.")

if(NOT EXISTS "${YGGDRASIL_NATIVE_PREFIX}")
    message(FATAL_ERROR "Native prefix does not exist: ${YGGDRASIL_NATIVE_PREFIX}")
endif()

option(YGGDRASIL_BUILD_TESTS "Build Yggdrasil C++ tests." OFF)

option(YGGDRASIL_ENABLE_FMT_FORMATTERS "Enable Yggdrasil's public fmt::formatter specializations." ON)
if(YGGDRASIL_ENABLE_FMT_FORMATTERS)
    add_compile_definitions(YGG_ENABLE_FMT_FORMATTERS=1)
else()
    add_compile_definitions(YGG_ENABLE_FMT_FORMATTERS=0)
endif()

add_subdirectory(python)

if(YGGDRASIL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

add_custom_target(pyyggdrasil_wheel DEPENDS pyyggdrasil_python)
