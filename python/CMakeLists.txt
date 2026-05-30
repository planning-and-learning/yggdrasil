set(YGGDRASIL_INSTALL_PACKAGEDIR "pyyggdrasil")

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/pyyggdrasil/__init__.py"
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
