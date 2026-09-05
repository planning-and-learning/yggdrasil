set(YGGDRASIL_INSTALL_PACKAGEDIR "pyyggdrasil")

# Dependencies

find_package(Python 3.11 REQUIRED
    COMPONENTS Interpreter Development.Module
    OPTIONAL_COMPONENTS Development.SABIModule
)

yggdrasil_source(YGGDRASIL_NANOBIND_VERSION nanobind version)
find_package(nanobind ${YGGDRASIL_NANOBIND_VERSION} CONFIG REQUIRED PATHS "${YGGDRASIL_NATIVE_PREFIX}" NO_DEFAULT_PATH)
find_package(TBB CONFIG REQUIRED PATHS "${YGGDRASIL_NATIVE_PREFIX}" NO_DEFAULT_PATH)

# Targets

nanobind_add_module(_pyyggdrasil BACKEND_MODULE nanobind_backend
    src/module.cpp
    src/pyyggdrasil/module.cpp
    src/pyyggdrasil/diagnostics/module.cpp
    src/pyyggdrasil/execution/module.cpp
)

target_compile_features(_pyyggdrasil PRIVATE cxx_std_20)

target_include_directories(_pyyggdrasil PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(_pyyggdrasil PRIVATE TBB::tbb)

if(APPLE)
    set_target_properties(_pyyggdrasil PROPERTIES
        BUILD_RPATH "${YGGDRASIL_NATIVE_PREFIX}/${CMAKE_INSTALL_LIBDIR}"
        INSTALL_RPATH "@loader_path/${CMAKE_INSTALL_LIBDIR}"
    )
elseif(UNIX)
    set_target_properties(_pyyggdrasil PROPERTIES
        BUILD_RPATH "${YGGDRASIL_NATIVE_PREFIX}/${CMAKE_INSTALL_LIBDIR}"
        INSTALL_RPATH "$ORIGIN/${CMAKE_INSTALL_LIBDIR}"
    )
endif()

# Installation and package export

set(PYYGGDRASIL_PYTHON_FILES
    pyyggdrasil/__init__.py
    pyyggdrasil/__main__.py
    pyyggdrasil/build_support.py
    pyyggdrasil/repair.py
    pyyggdrasil/py.typed
    pyyggdrasil/__init__.pyi
    pyyggdrasil/diagnostics/__init__.py
    pyyggdrasil/execution/__init__.py
)

foreach(python_file IN LISTS PYYGGDRASIL_PYTHON_FILES)
    get_filename_component(python_file_directory "${python_file}" DIRECTORY)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/${python_file}"
        DESTINATION "${python_file_directory}"
        COMPONENT pyyggdrasil
    )
endforeach()

install(DIRECTORY "${YGGDRASIL_NATIVE_PREFIX}/nanobind"
    DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}"
    COMPONENT pyyggdrasil
)

install(TARGETS _pyyggdrasil
    DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}"
    COMPONENT pyyggdrasil
)

install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/"
    DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}/include"
    COMPONENT pyyggdrasil
)

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/yggdrasilConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMinorVersion
)

set(YGGDRASIL_PACKAGE_ROOT "${YGGDRASIL_INSTALL_PACKAGEDIR}")
configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/Config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/yggdrasilConfig.cmake"
    INSTALL_DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}/${CMAKE_INSTALL_LIBDIR}/cmake/yggdrasil"
    PATH_VARS YGGDRASIL_PACKAGE_ROOT
    NO_CHECK_REQUIRED_COMPONENTS_MACRO
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/yggdrasilConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/yggdrasilConfigVersion.cmake"
    "${PROJECT_SOURCE_DIR}/cmake/yggdrasilPythonHelpers.cmake"
    "${PROJECT_SOURCE_DIR}/cmake/yggdrasilPatchPythonStubs.cmake"
    "${PROJECT_SOURCE_DIR}/cmake/yggdrasilFixRuntimePaths.cmake"
    "${PROJECT_SOURCE_DIR}/cmake/yggdrasilPythonPackaging.cmake"
    DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}/${CMAKE_INSTALL_LIBDIR}/cmake/yggdrasil"
    COMPONENT pyyggdrasil
)

# Ship the native dependency prefix inside the package, pruned to what
# downstream consumers need at build and run time. googletest and benchmark
# ARE bundled: downstream test/benchmark suites discover them from this prefix
# via find_package(GTest)/find_package(benchmark) with NO_DEFAULT_PATH, so they
# are pinned to the same versions as the rest of the toolchain. Still pruned:
# no static archives other than fmt's installed C target, no libtool files,
# no command line tools other than the minimal MPI toolchain, and one copy of
# each built Boost shared library.
foreach(native_dir IN ITEMS include lib lib64)
    if(EXISTS "${YGGDRASIL_NATIVE_PREFIX}/${native_dir}")
        install(DIRECTORY "${YGGDRASIL_NATIVE_PREFIX}/${native_dir}"
            DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}"
            COMPONENT pyyggdrasil
            REGEX "/pkgconfig$" EXCLUDE
            REGEX "\\.(a|la)$" EXCLUDE
            REGEX "/libboost_[^/]*\\.(so|dylib)[^/]*$" EXCLUDE
            REGEX "/lib(mpi|mpl|opa)[^/]*\\.(so|dylib)[^/]*$" EXCLUDE
        )
        file(GLOB boost_runtime_libraries
            LIST_DIRECTORIES false
            "${YGGDRASIL_NATIVE_PREFIX}/${native_dir}/libboost_*${CMAKE_SHARED_LIBRARY_SUFFIX}*"
        )
        foreach(boost_runtime_library IN LISTS boost_runtime_libraries)
            if(NOT IS_SYMLINK "${boost_runtime_library}")
                install(FILES "${boost_runtime_library}"
                    DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}/${native_dir}"
                    COMPONENT pyyggdrasil
                )
            endif()
        endforeach()
    endif()
endforeach()

if(EXISTS "${YGGDRASIL_NATIVE_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libfmt-c.a")
    install(FILES "${YGGDRASIL_NATIVE_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libfmt-c.a"
        DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}/${CMAKE_INSTALL_LIBDIR}"
        COMPONENT pyyggdrasil
    )
endif()

if(UNIX)
    if(APPLE)
        set(mpi_runtime_name "libmpi.dylib")
    else()
        set(mpi_runtime_name "libmpi.so")
    endif()
    file(REAL_PATH
        "${YGGDRASIL_NATIVE_PREFIX}/${CMAKE_INSTALL_LIBDIR}/${mpi_runtime_name}"
        mpi_runtime_library
    )
    install(FILES "${mpi_runtime_library}"
        DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}/${CMAKE_INSTALL_LIBDIR}"
        RENAME "${mpi_runtime_name}"
        COMPONENT pyyggdrasil
    )

    foreach(mpi_tool IN ITEMS mpicc mpicxx hydra_pmi_proxy)
        install(PROGRAMS "${YGGDRASIL_NATIVE_PREFIX}/bin/${mpi_tool}"
            DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}/bin"
            COMPONENT pyyggdrasil
        )
    endforeach()
    install(PROGRAMS "${YGGDRASIL_NATIVE_PREFIX}/bin/mpiexec.hydra"
        DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}/bin"
        RENAME mpiexec
        COMPONENT pyyggdrasil
    )
endif()

install(DIRECTORY "${YGGDRASIL_NATIVE_PREFIX}/share/licenses/"
    DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}/share/licenses"
    COMPONENT pyyggdrasil
)

install(FILES
    "${PROJECT_SOURCE_DIR}/docs/CORRESPONDING_SOURCE.md"
    "${PROJECT_SOURCE_DIR}/docs/SOURCES.json"
    DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}/share"
    COMPONENT pyyggdrasil
)

if(WIN32 AND EXISTS "${YGGDRASIL_NATIVE_PREFIX}/bin")
    # Runtime DLLs live in bin/ on Windows.
    install(DIRECTORY "${YGGDRASIL_NATIVE_PREFIX}/bin"
        DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}"
        COMPONENT pyyggdrasil
        FILES_MATCHING PATTERN "*.dll"
    )
endif()

install(CODE
    "include(\"${PROJECT_SOURCE_DIR}/cmake/yggdrasilFixRuntimePaths.cmake\")
         yggdrasil_fix_runtime_paths(LIB_DIR_GLOB \"pyyggdrasil/lib*\")"
    COMPONENT pyyggdrasil
)

nanobind_add_stub(
    pyyggdrasil_stubs
    MODULE pyyggdrasil._pyyggdrasil
    RECURSIVE
    INSTALL_TIME
    OUTPUT_PATH "pyyggdrasil"
    PYTHON_PATH "."
    COMPONENT pyyggdrasil
)

install(CODE
    "include(\"${PROJECT_SOURCE_DIR}/cmake/yggdrasilPatchPythonStubs.cmake\")
         yggdrasil_patch_python_stubs(PACKAGE pyyggdrasil PRIVATE_MODULE _pyyggdrasil RENAME_PACKAGES pyyggdrasil)"
    COMPONENT pyyggdrasil
)

add_custom_target(pyyggdrasil_python DEPENDS _pyyggdrasil)
