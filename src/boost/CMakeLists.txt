cmake_minimum_required(VERSION 3.21)
project(InstallBoost)

include(ExternalProject)

set(YGGDRASIL_BOOST_VERSION 1.84.0 CACHE STRING "Boost version used to build boost.")
string(REPLACE "." "_" YGGDRASIL_BOOST_ARCHIVE_VERSION ${YGGDRASIL_BOOST_VERSION})

message(STATUS "Preparing external project \"boost\":")

set(BOOST_USER_CONFIG "${CMAKE_CURRENT_BINARY_DIR}/user-config.jam")
file(WRITE "${BOOST_USER_CONFIG}" "using mpi : \"${CMAKE_INSTALL_PREFIX}/bin/mpicxx\" ;\n")

# We need to build boost for cmake support
ExternalProject_Add(
    boost
    DEPENDS mpich
    URL https://archives.boost.io/release/${YGGDRASIL_BOOST_VERSION}/source/boost_${YGGDRASIL_BOOST_ARCHIVE_VERSION}.tar.gz
    URL_HASH SHA256=a5800f405508f5df8114558ca9855d2640a2de8f0445f051fa1c7c3383045724
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    PREFIX ${CMAKE_BINARY_DIR}/boost
    STAMP_DIR ${CMAKE_BINARY_DIR}/boost/stamp/${YGGDRASIL_BOOST_VERSION}
    CONFIGURE_COMMAND
        ${CMAKE_COMMAND} -E chdir <SOURCE_DIR>
        ./bootstrap.sh
        --prefix=<INSTALL_DIR>
        --with-libraries=container,iostreams,json,mpi,serialization
    BUILD_COMMAND ""
    INSTALL_COMMAND
        ${CMAKE_COMMAND} -E chdir <SOURCE_DIR>
        ./b2
        -j${YGGDRASIL_JOBS}
        --ignore-site-config
        --user-config=${BOOST_USER_CONFIG}
        install
        variant=release
        threading=multi
        link=shared
        runtime-link=shared
        hardcode-dll-paths=true
        dll-path=${YGGDRASIL_DEPENDENCY_INSTALL_RPATH}
    INSTALL_DIR ${CMAKE_INSTALL_PREFIX}
)
