cmake_minimum_required(VERSION 3.21)
project(InstallNanobindSharedRuntime LANGUAGES CXX)

include(GNUInstallDirs)

find_package(Python 3.8 REQUIRED
  COMPONENTS Interpreter Development.Module
  OPTIONAL_COMPONENTS Development.SABIModule)

# Build from the upstream datadir installed by the nanobind external project;
# it still contains the runtime sources (src/, ext/robin_map).
find_package(nanobind CONFIG REQUIRED
             PATHS "${CMAKE_INSTALL_PREFIX}/nanobind/cmake" NO_DEFAULT_PATH)

nanobind_build_library(nanobind)
add_custom_target(nanobind_shared_runtime ALL DEPENDS nanobind)

install(TARGETS nanobind
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

# Flatten the nanobind datadir into the standard prefix layout used by the
# other bundled dependencies. src/ and ext/robin_map are dropped: they are only
# needed to compile the runtime, which ships prebuilt.
install(DIRECTORY "${NB_DIR}/include/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

# The upstream config goes verbatim into a cmake/ subdirectory; find_package
# loads our wrapper, which includes it and defines the IMPORTED `nanobind`
# target for the prebuilt shared runtime. NB_DIR inside the upstream config
# resolves to <libdir>/cmake/nanobind, so its relative lookups (the darwin
# files in cmake/ and stubgen.py, used by nanobind_add_stub) work unchanged.
install(FILES
        "${NB_DIR}/cmake/nanobind-config.cmake"
        "${NB_DIR}/cmake/darwin-python-path.py"
        "${NB_DIR}/cmake/darwin-ld-cpython.sym"
        "${NB_DIR}/cmake/darwin-ld-pypy.sym"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/nanobind/cmake")

install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/nanobind-config.cmake"
        "${NB_DIR}/cmake/nanobind-config-version.cmake"
        "${NB_DIR}/stubgen.py"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/nanobind")

# Drop the upstream datadir; everything consumers need now lives in the
# standard locations.
install(CODE "file(REMOVE_RECURSE \"\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/nanobind\")")
