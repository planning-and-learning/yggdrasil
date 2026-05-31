cmake_minimum_required(VERSION 3.21)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/pyproject.toml" YGGDRASIL_PROJECT_VERSION_LINE REGEX "^version[ \t]*=" LIMIT_COUNT 1)
if(NOT YGGDRASIL_PROJECT_VERSION_LINE)
    message(FATAL_ERROR "Could not read project version from pyproject.toml")
endif()
string(REGEX REPLACE "^version[ \t]*=[ \t]*\"([^\"]+)\".*$" "\\1" YGGDRASIL_PROJECT_VERSION "${YGGDRASIL_PROJECT_VERSION_LINE}")

project(yggdrasil VERSION "${YGGDRASIL_PROJECT_VERSION}")

include(GNUInstallDirs)

set(YGGDRASIL_NATIVE_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/dependencies-install" CACHE PATH "Native dependency prefix to package.")

if(NOT EXISTS "${YGGDRASIL_NATIVE_PREFIX}")
    message(FATAL_ERROR "Native prefix does not exist: ${YGGDRASIL_NATIVE_PREFIX}")
endif()

add_subdirectory(python)

add_custom_target(pyyggdrasil_wheel DEPENDS pyyggdrasil_python)
