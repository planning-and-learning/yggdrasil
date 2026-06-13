set(YGGDRASIL_INSTALL_PACKAGEDIR "pyyggdrasil")

find_package(Python 3.8 REQUIRED
  COMPONENTS Interpreter Development.Module
  OPTIONAL_COMPONENTS Development.SABIModule)

find_package(nanobind CONFIG REQUIRED PATHS "${YGGDRASIL_NATIVE_PREFIX}" NO_DEFAULT_PATH)
find_package(TBB CONFIG REQUIRED PATHS "${YGGDRASIL_NATIVE_PREFIX}" NO_DEFAULT_PATH)

nanobind_add_module(_pyyggdrasil NB_SHARED
    src/module.cpp
    src/pyyggdrasil/module.cpp
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
        INSTALL_RPATH "@loader_path/${CMAKE_INSTALL_LIBDIR}")
elseif(UNIX)
    set_target_properties(_pyyggdrasil PROPERTIES
        BUILD_RPATH "${YGGDRASIL_NATIVE_PREFIX}/${CMAKE_INSTALL_LIBDIR}"
        INSTALL_RPATH "$ORIGIN/${CMAKE_INSTALL_LIBDIR}")
endif()

set(PYYGGDRASIL_PYTHON_FILES
    pyyggdrasil/__init__.py
    pyyggdrasil/__main__.py
    pyyggdrasil/build_support.py
    pyyggdrasil/repair.py
    pyyggdrasil/__init__.pyi
    pyyggdrasil/execution/__init__.pyi
)

foreach(python_file IN LISTS PYYGGDRASIL_PYTHON_FILES)
    get_filename_component(python_file_directory "${python_file}" DIRECTORY)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/${python_file}"
            DESTINATION "${python_file_directory}"
            COMPONENT pyyggdrasil)
endforeach()

install(TARGETS _pyyggdrasil
        DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}"
        COMPONENT pyyggdrasil)

install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/"
        DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}/include"
        COMPONENT pyyggdrasil)

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/yggdrasilConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMinorVersion)

set(YGGDRASIL_PACKAGE_ROOT "${YGGDRASIL_INSTALL_PACKAGEDIR}")
configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/Config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/yggdrasilConfig.cmake"
    INSTALL_DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}/${CMAKE_INSTALL_LIBDIR}/cmake/yggdrasil"
    PATH_VARS YGGDRASIL_PACKAGE_ROOT
    NO_CHECK_REQUIRED_COMPONENTS_MACRO)

install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/yggdrasilConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/yggdrasilConfigVersion.cmake"
        "${PROJECT_SOURCE_DIR}/cmake/yggdrasilPythonHelpers.cmake"
        "${PROJECT_SOURCE_DIR}/cmake/yggdrasilPatchPythonStubs.cmake"
        "${PROJECT_SOURCE_DIR}/cmake/yggdrasilFixRuntimePaths.cmake"
        "${PROJECT_SOURCE_DIR}/cmake/yggdrasilPythonPackaging.cmake"
        DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}/${CMAKE_INSTALL_LIBDIR}/cmake/yggdrasil"
        COMPONENT pyyggdrasil)

# Ship the native dependency prefix inside the package, pruned to what
# downstream consumers need at build and run time: no test-only dependencies
# (googletest, benchmark), no static archives or libtool files, no nauty
# command line tools, and only the Boost runtime libraries that are actually
# linked downstream: json, iostreams, and container (a CMake-level dependency
# of Boost::json).
foreach(native_dir IN ITEMS include lib lib64)
    if(EXISTS "${YGGDRASIL_NATIVE_PREFIX}/${native_dir}")
        install(DIRECTORY "${YGGDRASIL_NATIVE_PREFIX}/${native_dir}"
                DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}"
                COMPONENT pyyggdrasil
                REGEX "/include/(gtest|gmock|benchmark)$" EXCLUDE
                REGEX "/lib(gtest|gmock|benchmark)[^/]*$" EXCLUDE
                REGEX "/cmake/(GTest|benchmark)$" EXCLUDE
                REGEX "/pkgconfig$" EXCLUDE
                REGEX "\\.(a|la)$" EXCLUDE
                REGEX "/libboost_[^/]*\\.(so|dylib)[^/]*$" EXCLUDE)
        # Re-add the linked Boost runtime libraries; install(DIRECTORY)
        # preserves the symlink chains.
        install(DIRECTORY "${YGGDRASIL_NATIVE_PREFIX}/${native_dir}"
                DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}"
                COMPONENT pyyggdrasil
                FILES_MATCHING
                REGEX "/libboost_(json|iostreams|container)[^/]*\\.(so|dylib)[^/]*$"
                REGEX "/cmake$" EXCLUDE
                REGEX "/pkgconfig$" EXCLUDE)
    endif()
endforeach()

if(WIN32 AND EXISTS "${YGGDRASIL_NATIVE_PREFIX}/bin")
    # Runtime DLLs live in bin/ on Windows.
    install(DIRECTORY "${YGGDRASIL_NATIVE_PREFIX}/bin"
            DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}"
            COMPONENT pyyggdrasil
            FILES_MATCHING PATTERN "*.dll")
endif()

install(CODE
        "include(\"${PROJECT_SOURCE_DIR}/cmake/yggdrasilFixRuntimePaths.cmake\")
         yggdrasil_fix_runtime_paths(LIB_DIR_GLOB \"pyyggdrasil/lib*\")"
        COMPONENT pyyggdrasil)

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
        COMPONENT pyyggdrasil)

add_custom_target(pyyggdrasil_python DEPENDS _pyyggdrasil)
