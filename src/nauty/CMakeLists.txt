cmake_minimum_required(VERSION 3.21)
project(InstallNauty)

include(ExternalProject)

yggdrasil_source(YGGDRASIL_NAUTY_VERSION nauty version)
yggdrasil_source(YGGDRASIL_NAUTY_URLS nauty urls)
yggdrasil_source(YGGDRASIL_NAUTY_SHA256 nauty sha256)

message(STATUS "Dependency \"nauty\"...")

find_package(Nauty QUIET PATHS ${CMAKE_INSTALL_PREFIX} NO_DEFAULT_PATH)

if(NOT Nauty_FOUND)
    # Nauty's makefile still expects the static archive to exist while building shared libraries.
    # We build both artifacts here, but the installed CMake package exposes Nauty::Nauty as shared.
    set(NAUTY_SHARED_ARGS --enable-shared --enable-static --enable-generic --disable-popcnt)
    set(NAUTY_LIBRARY_TYPE SHARED)
    set(NAUTY_LIBRARY_FILENAME "${CMAKE_SHARED_LIBRARY_PREFIX}nauty${CMAKE_SHARED_LIBRARY_SUFFIX}")

    message(STATUS "Preparing external project \"nauty\" with args:")
    message(STATUS "-- ${NAUTY_SHARED_ARGS}")

    # We need to build nauty for cmake support
    ExternalProject_Add(
        nauty
        URL ${YGGDRASIL_NAUTY_URLS}
        URL_HASH SHA256=${YGGDRASIL_NAUTY_SHA256}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        PREFIX ${CMAKE_BINARY_DIR}/nauty
        STAMP_DIR ${CMAKE_BINARY_DIR}/nauty/stamp/${YGGDRASIL_NAUTY_VERSION}
        STEP_TARGETS download
        CONFIGURE_COMMAND ${CMAKE_COMMAND} -E chdir <SOURCE_DIR> ./configure --prefix=<INSTALL_DIR> CFLAGS=-fPIC --enable-tls ${NAUTY_SHARED_ARGS}
        BUILD_COMMAND ${CMAKE_COMMAND} -E chdir <SOURCE_DIR> make -j${YGGDRASIL_JOBS}
        INSTALL_COMMAND ${CMAKE_COMMAND} -E chdir <SOURCE_DIR> make -j${YGGDRASIL_JOBS} install
        INSTALL_DIR ${CMAKE_INSTALL_PREFIX}
    )

    ExternalProject_Add_Step(
        nauty install-cmake-package
        COMMAND ${CMAKE_COMMAND}
                -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
                -DCMAKE_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
                -DNAUTY_LIBRARY_TYPE=${NAUTY_LIBRARY_TYPE}
                -DNAUTY_LIBRARY_FILENAME=${NAUTY_LIBRARY_FILENAME}
                -S ${CMAKE_CURRENT_LIST_DIR}/package
                -B ${CMAKE_BINARY_DIR}/nauty-package
        COMMAND ${CMAKE_COMMAND}
                --install ${CMAKE_BINARY_DIR}/nauty-package
        DEPENDEES install
    )
else()
    message(STATUS "nauty is already installed.")
endif()
