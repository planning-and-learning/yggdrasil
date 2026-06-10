cmake_minimum_required(VERSION 3.21)
project(InstallBoost)

include(ExternalProject)

set(YGGDRASIL_BOOST_VERSION 1.84.0 CACHE STRING "Boost version used to build boost.")
string(REPLACE "." "_" YGGDRASIL_BOOST_ARCHIVE_VERSION ${YGGDRASIL_BOOST_VERSION})

message(STATUS "Preparing external project \"boost\":")

set(BOOST_LINK shared)
set(BOOST_RUNTIME_LINK shared)
set(BOOST_DLL_PATH_ARGS hardcode-dll-paths=true dll-path=${YGGDRASIL_DEPENDENCY_INSTALL_RPATH})

# We need to build boost for cmake support
ExternalProject_Add(
    boost
    URL https://archives.boost.io/release/${YGGDRASIL_BOOST_VERSION}/source/boost_${YGGDRASIL_BOOST_ARCHIVE_VERSION}.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    PREFIX ${CMAKE_BINARY_DIR}/boost
    STAMP_DIR ${CMAKE_BINARY_DIR}/boost/stamp/${YGGDRASIL_BOOST_VERSION}
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E chdir <SOURCE_DIR> ./bootstrap.sh --prefix=<INSTALL_DIR>
    BUILD_COMMAND ${CMAKE_COMMAND} -E chdir <SOURCE_DIR> ./b2 headers
    INSTALL_COMMAND ${CMAKE_COMMAND} -E chdir <SOURCE_DIR> ./b2 install --without-python link=${BOOST_LINK} runtime-link=${BOOST_RUNTIME_LINK} ${BOOST_DLL_PATH_ARGS}
    INSTALL_DIR ${CMAKE_INSTALL_PREFIX}
)
