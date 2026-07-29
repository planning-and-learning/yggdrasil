cmake_minimum_required(VERSION 3.21)
project(InstallMPICH)

include(ExternalProject)

set(YGGDRASIL_MPICH_VERSION 5.0.1 CACHE STRING "MPICH version used to build MPI.")

message(STATUS "Dependency \"mpich\"...")

set(MPICH_CONFIGURE_ARGS
    --disable-option-checking
    --enable-shared
    --disable-static
    --enable-versioning
    --disable-fortran
    --disable-cxx
    --disable-romio
    --disable-doc
    --disable-dependency-tracking
    --enable-fast=O2,ndebug
    --enable-threads=multiple
    --with-device=ch4:ofi:tcp
    --with-libfabric=embedded
    --enable-only
    --without-uring
    --with-datatype-engine=dataloop
    --without-hwloc
    --with-hydra-topolib=no
    --with-pm=hydra
    --with-pmilib=mpich
    --with-hydra-bss=user,ssh,fork,slurm
    --with-wrapper-dl-type=runpath
    --without-cuda
    --without-hip
    --without-ze
    --without-hcoll
    --without-ucc
    --without-nccl
    --without-rccl
    --without-xpmem
    --without-netloc
    --without-valgrind
)

message(STATUS "Preparing external project \"mpich\" with args:")
foreach (MPICH_CONFIGURE_ARG IN LISTS MPICH_CONFIGURE_ARGS)
    message(STATUS "-- ${MPICH_CONFIGURE_ARG}")
endforeach ()

if(APPLE)
    # Keep the versioned install name while omitting a duplicate linker alias.
    set(MPICH_MPI_LIBRARY libmpi.12.dylib)
endif()

ExternalProject_Add(
    mpich
    URL https://github.com/pmodels/mpich/releases/download/v${YGGDRASIL_MPICH_VERSION}/mpich-${YGGDRASIL_MPICH_VERSION}.tar.gz
    URL_HASH SHA256=8c1832a13ddacf071685069f5fadfd1f2877a29e1a628652892c65211b1f3327
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    PREFIX ${CMAKE_BINARY_DIR}/mpich
    STAMP_DIR ${CMAKE_BINARY_DIR}/mpich/stamp/${YGGDRASIL_MPICH_VERSION}
    PATCH_COMMAND
        ${CMAKE_COMMAND}
        -DMPICH_SOURCE_DIR=<SOURCE_DIR>
        -DMPICH_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
        -DMPICH_MPI_LIBRARY=${MPICH_MPI_LIBRARY}
        -P ${CMAKE_CURRENT_LIST_DIR}/patch_wrappers.cmake
    CONFIGURE_COMMAND
        ${CMAKE_COMMAND} -E env
        CC=${CMAKE_C_COMPILER}
        CXX=${CMAKE_CXX_COMPILER}
        <SOURCE_DIR>/configure
        --prefix=<INSTALL_DIR>
        --libdir=<INSTALL_DIR>/${CMAKE_INSTALL_LIBDIR}
        ${MPICH_CONFIGURE_ARGS}
    BUILD_COMMAND make -j${YGGDRASIL_JOBS}
    INSTALL_COMMAND make -j${YGGDRASIL_JOBS} install
    INSTALL_DIR ${CMAKE_INSTALL_PREFIX}
)
