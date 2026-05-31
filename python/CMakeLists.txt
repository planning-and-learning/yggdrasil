set(YGGDRASIL_INSTALL_PACKAGEDIR "pyyggdrasil")

find_package(Python 3.8 REQUIRED
  COMPONENTS Interpreter Development.Module
  OPTIONAL_COMPONENTS Development.SABIModule)

find_package(nanobind CONFIG REQUIRED PATHS "${YGGDRASIL_NATIVE_PREFIX}" NO_DEFAULT_PATH)
find_package(TBB CONFIG REQUIRED PATHS "${YGGDRASIL_NATIVE_PREFIX}" NO_DEFAULT_PATH)

nanobind_add_module(_pyyggdrasil
    src/module.cpp
    src/pyyggdrasil/module.cpp
)

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

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/pyyggdrasil/__init__.py"
        DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}"
        COMPONENT pyyggdrasil)

install(TARGETS _pyyggdrasil
        DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}"
        COMPONENT pyyggdrasil)

install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/"
        DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}/include"
        COMPONENT pyyggdrasil)

foreach(native_dir IN ITEMS include lib lib64 bin nanobind)
    if(EXISTS "${YGGDRASIL_NATIVE_PREFIX}/${native_dir}")
        install(DIRECTORY "${YGGDRASIL_NATIVE_PREFIX}/${native_dir}"
                DESTINATION "${YGGDRASIL_INSTALL_PACKAGEDIR}"
                COMPONENT pyyggdrasil)
    endif()
endforeach()

if(APPLE)
    install(SCRIPT "${PROJECT_SOURCE_DIR}/cmake/install/fix_macos_runtime_paths.cmake"
            COMPONENT pyyggdrasil)
elseif(UNIX)
    install(SCRIPT "${PROJECT_SOURCE_DIR}/cmake/install/fix_unix_runtime_paths.cmake"
            COMPONENT pyyggdrasil)
endif()

add_custom_target(pyyggdrasil_python DEPENDS _pyyggdrasil)
