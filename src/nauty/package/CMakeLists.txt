cmake_minimum_required(VERSION 3.21)

include("${CMAKE_CURRENT_LIST_DIR}/../../source_manifest.cmake")
yggdrasil_source(YGGDRASIL_NAUTY_VERSION nauty version)
project(NautyPackage VERSION ${YGGDRASIL_NAUTY_VERSION} LANGUAGES C)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set(NAUTY_LIBRARY_TYPE SHARED CACHE STRING "Imported Nauty library type.")
set(NAUTY_LIBRARY_FILENAME "${CMAKE_SHARED_LIBRARY_PREFIX}nauty${CMAKE_SHARED_LIBRARY_SUFFIX}" CACHE STRING "Installed Nauty library filename.")

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/NautyConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/NautyConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Nauty")

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/NautyConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/NautyConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/NautyConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Nauty")
