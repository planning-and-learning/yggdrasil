cmake_minimum_required(VERSION 3.21)
project(InstallBoost)

include(ExternalProject)

yggdrasil_source(YGGDRASIL_BOOST_VERSION boost version)
yggdrasil_source(YGGDRASIL_BOOST_URLS boost urls)
yggdrasil_source(YGGDRASIL_BOOST_SHA256 boost sha256)

message(STATUS "Preparing external project \"boost\":")

set(BOOST_USER_CONFIG "${CMAKE_CURRENT_BINARY_DIR}/user-config.jam")
file(WRITE "${BOOST_USER_CONFIG}"
    "using mpi : : <include>${CMAKE_INSTALL_PREFIX}/include <library-path>${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR} <find-shared-library>mpi ;\n"
)

# We need to build boost for cmake support
ExternalProject_Add(
    boost
    DEPENDS mpich
    URL ${YGGDRASIL_BOOST_URLS}
    URL_HASH SHA256=${YGGDRASIL_BOOST_SHA256}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    PREFIX ${CMAKE_BINARY_DIR}/boost
    STAMP_DIR ${CMAKE_BINARY_DIR}/boost/stamp/${YGGDRASIL_BOOST_VERSION}
    STEP_TARGETS download
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
