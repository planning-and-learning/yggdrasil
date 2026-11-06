set(YGGDRASIL_INSTALL_PACKAGEDIR "pyyggdrasil")

# Dependencies

find_package(Python 3.11 REQUIRED
    COMPONENTS Interpreter Development.Module
    OPTIONAL_COMPONENTS Development.SABIModule
)

find_package(nanobind CONFIG REQUIRED PATHS "${URD_NATIVE_PREFIX}" NO_DEFAULT_PATH)
find_package(TBB CONFIG REQUIRED PATHS "${URD_NATIVE_PREFIX}" NO_DEFAULT_PATH)
find_package(Boost CONFIG REQUIRED COMPONENTS json PATHS "${URD_NATIVE_PREFIX}" NO_DEFAULT_PATH)
find_package(fmt CONFIG REQUIRED PATHS "${URD_NATIVE_PREFIX}" NO_DEFAULT_PATH)
find_package(cista CONFIG REQUIRED PATHS "${URD_NATIVE_PREFIX}" NO_DEFAULT_PATH)

# Targets

file(GLOB_RECURSE YGGDRASIL_PUBLIC_HEADERS CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/include/yggdrasil/*.hpp"
)

nanobind_add_module(_pyyggdrasil NB_SUPPRESS_WARNINGS BACKEND_MODULE nanobind_backend
    src/module.cpp
    src/pyyggdrasil/module.cpp
    src/pyyggdrasil/database/module.cpp
    src/pyyggdrasil/database/distance.cpp
    src/pyyggdrasil/database/evaluation.cpp
    src/pyyggdrasil/database/optimization.cpp
    src/pyyggdrasil/database/query/query.cpp
    src/pyyggdrasil/database/query/input.cpp
    src/pyyggdrasil/database/query/empty.cpp
    src/pyyggdrasil/database/query/join.cpp
    src/pyyggdrasil/database/query/project.cpp
    src/pyyggdrasil/database/query/rename.cpp
    src/pyyggdrasil/database/query/select_equal.cpp
    src/pyyggdrasil/database/query/select_value.cpp
    src/pyyggdrasil/database/query/union.cpp
    src/pyyggdrasil/database/query/difference.cpp
    src/pyyggdrasil/database/query/distance.cpp
    src/pyyggdrasil/database/query/generic_join.cpp
    src/pyyggdrasil/database/query/semijoin.cpp
    src/pyyggdrasil/database/query/antijoin.cpp
    src/pyyggdrasil/diagnostics/module.cpp
    src/pyyggdrasil/execution/module.cpp
    src/pyyggdrasil/serialization/module.cpp
    ${YGGDRASIL_PUBLIC_HEADERS}
)

target_compile_features(_pyyggdrasil PRIVATE cxx_std_20)

target_include_directories(_pyyggdrasil PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(_pyyggdrasil PRIVATE yggdrasil::database TBB::tbb Boost::json fmt::fmt cista::cista)

if(APPLE)
    set_target_properties(_pyyggdrasil PROPERTIES
        BUILD_RPATH "${URD_NATIVE_PREFIX}/${CMAKE_INSTALL_LIBDIR}"
        INSTALL_RPATH "@loader_path/${CMAKE_INSTALL_LIBDIR};@loader_path/../pyurd/${CMAKE_INSTALL_LIBDIR}"
    )
elseif(UNIX)
    set_target_properties(_pyyggdrasil PROPERTIES
        BUILD_RPATH "${URD_NATIVE_PREFIX}/${CMAKE_INSTALL_LIBDIR}"
        INSTALL_RPATH "$ORIGIN/${CMAKE_INSTALL_LIBDIR};$ORIGIN/../pyurd/${CMAKE_INSTALL_LIBDIR}"
    )
endif()

# Installation and package export

set(PYYGGDRASIL_PYTHON_FILES
    pyyggdrasil/__init__.py
    pyyggdrasil/__main__.py
    pyyggdrasil/serialization/__init__.py
    pyyggdrasil/serialization/table.py
    pyyggdrasil/py.typed
    pyyggdrasil/__init__.pyi
    pyyggdrasil/database/__init__.py
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

install(TARGETS _pyyggdrasil
    DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}"
    COMPONENT pyyggdrasil
)

install(
    TARGETS database
    EXPORT yggdrasilDatabaseTargets
    LIBRARY DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}/${CMAKE_INSTALL_LIBDIR}" COMPONENT pyyggdrasil
    ARCHIVE DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}/${CMAKE_INSTALL_LIBDIR}" COMPONENT pyyggdrasil
    RUNTIME DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}/${CMAKE_INSTALL_BINDIR}" COMPONENT pyyggdrasil
)

install(
    EXPORT yggdrasilDatabaseTargets
    NAMESPACE yggdrasil::
    FILE "yggdrasilDatabaseTargets.cmake"
    DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}/${CMAKE_INSTALL_LIBDIR}/cmake/yggdrasil"
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
    DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}/${CMAKE_INSTALL_LIBDIR}/cmake/yggdrasil"
    COMPONENT pyyggdrasil
)

# The native dependencies are pyurd's: the wheel ships yggdrasil's own files only.

install(CODE
    "include(\"${brokkr_DIR}/brokkrFixRuntimePaths.cmake\")
         brokkr_fix_runtime_paths(LIB_DIR_GLOB \"pyyggdrasil/lib*\"
             RPATH \"\$ORIGIN:\$ORIGIN/../../pyurd/${CMAKE_INSTALL_LIBDIR}\"
             RPATHS \"@loader_path;@loader_path/../../pyurd/${CMAKE_INSTALL_LIBDIR}\")"
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
    "include(\"${brokkr_DIR}/brokkrPatchPythonStubs.cmake\")
         brokkr_patch_python_stubs(PACKAGE pyyggdrasil PRIVATE_MODULE _pyyggdrasil RENAME_PACKAGES pyyggdrasil)"
    COMPONENT pyyggdrasil
)

add_custom_target(pyyggdrasil_python DEPENDS _pyyggdrasil)
