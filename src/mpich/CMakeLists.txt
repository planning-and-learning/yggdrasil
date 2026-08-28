cmake_minimum_required(VERSION 3.21)
project(InstallMPICH)

include(ExternalProject)

yggdrasil_source(YGGDRASIL_MPICH_VERSION mpich version)
yggdrasil_source(YGGDRASIL_MPICH_URLS mpich urls)
yggdrasil_source(YGGDRASIL_MPICH_SHA256 mpich sha256)

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
    URL ${YGGDRASIL_MPICH_URLS}
    URL_HASH SHA256=${YGGDRASIL_MPICH_SHA256}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    PREFIX ${CMAKE_BINARY_DIR}/mpich
    STAMP_DIR ${CMAKE_BINARY_DIR}/mpich/stamp/${YGGDRASIL_MPICH_VERSION}
    STEP_TARGETS download
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
