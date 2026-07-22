# Wheel-packaging macros shared by the provider wheels (pypddl, pytyr,
# pyrunir). Shipped with the yggdrasil CMake package and included from
# yggdrasilConfig.cmake, so they become available after find_package(yggdrasil).
#
# The macros encode the family conventions: the wheel ships the python package
# <pkg> with the nanobind module <pkg>/_<pkg>, the repo's native libraries
# under <pkg>/native/{include,lib,bin}, and the repo's exported CMake package
# under <pkg>/native/lib/cmake/<name>/. Runtime resolution happens through
# relative rpaths into the sibling provider packages registered with
# yggdrasil_register_python_native_runtime_prefix().

include(CMakePackageConfigHelpers)

# Defines <PKG>_NATIVE_{DIR,INCLUDEDIR,LIBDIR,BINDIR,CMAKEDIR} for the calling
# scope, e.g. yggdrasil_python_package_init(PYPDDL pypddl).
macro(yggdrasil_python_package_init PKG_UPPER package)
    set(${PKG_UPPER}_NATIVE_DIR "${package}/native")
    set(${PKG_UPPER}_NATIVE_INCLUDEDIR "${${PKG_UPPER}_NATIVE_DIR}/${CMAKE_INSTALL_INCLUDEDIR}")
    set(${PKG_UPPER}_NATIVE_LIBDIR "${${PKG_UPPER}_NATIVE_DIR}/${CMAKE_INSTALL_LIBDIR}")
    set(${PKG_UPPER}_NATIVE_BINDIR "${${PKG_UPPER}_NATIVE_DIR}/${CMAKE_INSTALL_BINDIR}")
    set(${PKG_UPPER}_NATIVE_CMAKEDIR "${${PKG_UPPER}_NATIVE_LIBDIR}/cmake")
endmacro()

# Computes the conventional rpaths and applies BUILD/INSTALL_RPATH to the
# nanobind module target. Defines in the calling scope:
#   <PKG>_MODULE_RPATH                   (module: origin + ../<providers> + native libdir)
#   <PKG>_CORE_RPATH                     (native libs: origin + ../../../<providers>)
#   <PKG>_INSTALL_NATIVE_LIBRARY_RPATH   (colon-joined CORE_RPATH, for patchelf)
#   <PKG>_INSTALL_NATIVE_LIBRARY_RPATHS  (CORE_RPATH list, for install_name_tool)
macro(yggdrasil_python_module_rpaths PKG_UPPER module_target)
    if(APPLE)
        set(_yggdrasil_rpath_origin "@loader_path")
    else()
        set(_yggdrasil_rpath_origin "$ORIGIN")
    endif()

    yggdrasil_make_python_native_runtime_rpaths(${PKG_UPPER}_MODULE_RPATH "${_yggdrasil_rpath_origin}" "../")
    list(APPEND ${PKG_UPPER}_MODULE_RPATH "${_yggdrasil_rpath_origin}/native/${CMAKE_INSTALL_LIBDIR}")
    yggdrasil_make_python_native_runtime_rpaths(${PKG_UPPER}_CORE_RPATH "${_yggdrasil_rpath_origin}" "../../../")
    set(${PKG_UPPER}_INSTALL_NATIVE_LIBRARY_RPATHS "${${PKG_UPPER}_CORE_RPATH}")
    yggdrasil_make_python_native_runtime_rpath_string(
        ${PKG_UPPER}_INSTALL_NATIVE_LIBRARY_RPATH
        "${_yggdrasil_rpath_origin}"
        "../../../"
    )

    set_target_properties(${module_target} PROPERTIES
        BUILD_RPATH "${${PKG_UPPER}_MODULE_RPATH}"
        INSTALL_RPATH "${${PKG_UPPER}_MODULE_RPATH}"
    )
endmacro()

# Installs the repo's native library targets with their CMake export package
# under <pkg>/native/lib/cmake/<config_name>/:
#   yggdrasil_install_native_export(
#       PKG_UPPER <PYPDDL> PACKAGE <pypddl> CONFIG_NAME <loki> NAMESPACE <loki::>
#       EXPORT_NAME <pypddlLokiparsersTargets> EXPORT_FILE <lokiparsersTargets.cmake>
#       TARGETS <parsers;...> HEADER_DIRS <abs-dir;...>
#       CONFIG_TEMPLATE <abs Config.cmake.in> VERSION_FILE <abs ConfigVersion.cmake>
#       CMAKE_SOURCE_DIRECTORY <abs cmake/ dir>)
# Each target gets INSTALL_RPATH = <PKG>_CORE_RPATH and the registered
# dependency include dirs appended to its build interface.
function(yggdrasil_install_native_export)
    cmake_parse_arguments(
        PARSE_ARGV 0
        ARG
        ""
        "PKG_UPPER;PACKAGE;CONFIG_NAME;NAMESPACE;EXPORT_NAME;EXPORT_FILE;CONFIG_TEMPLATE;VERSION_FILE;CMAKE_SOURCE_DIRECTORY"
        "TARGETS;HEADER_DIRS"
    )
    foreach(
        required
        IN ITEMS PKG_UPPER PACKAGE CONFIG_NAME NAMESPACE EXPORT_NAME EXPORT_FILE CONFIG_TEMPLATE VERSION_FILE
    )
        if(NOT ARG_${required})
            message(FATAL_ERROR "yggdrasil_install_native_export: ${required} is required")
        endif()
    endforeach()

    set(native_libdir "${${ARG_PKG_UPPER}_NATIVE_LIBDIR}")
    set(native_bindir "${${ARG_PKG_UPPER}_NATIVE_BINDIR}")
    set(native_includedir "${${ARG_PKG_UPPER}_NATIVE_INCLUDEDIR}")
    set(native_cmakedir "${${ARG_PKG_UPPER}_NATIVE_CMAKEDIR}")
    set(core_rpath "${${ARG_PKG_UPPER}_CORE_RPATH}")

    foreach(native_target IN LISTS ARG_TARGETS)
        if(core_rpath)
            set_property(TARGET ${native_target} PROPERTY INSTALL_RPATH "${core_rpath}")
        endif()
        if(YGGDRASIL_NATIVE_DEPENDENCY_INCLUDE_DIRECTORIES)
            set_property(TARGET ${native_target} APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                "$<BUILD_INTERFACE:${YGGDRASIL_NATIVE_DEPENDENCY_INCLUDE_DIRECTORIES}>"
            )
        endif()
        install(
            TARGETS ${native_target}
            EXPORT ${ARG_EXPORT_NAME}
            LIBRARY DESTINATION "${native_libdir}" COMPONENT ${ARG_PACKAGE}
            ARCHIVE DESTINATION "${native_libdir}" COMPONENT ${ARG_PACKAGE}
            RUNTIME DESTINATION "${native_bindir}" COMPONENT ${ARG_PACKAGE}
            INCLUDES DESTINATION "${native_includedir}"
        )
    endforeach()

    foreach(header_dir IN LISTS ARG_HEADER_DIRS)
        install(DIRECTORY "${header_dir}"
            DESTINATION "${native_includedir}"
            COMPONENT ${ARG_PACKAGE}
        )
    endforeach()

    configure_package_config_file(
        "${ARG_CONFIG_TEMPLATE}"
        "${CMAKE_CURRENT_BINARY_DIR}/${ARG_CONFIG_NAME}Config.cmake"
        INSTALL_DESTINATION "${native_cmakedir}/${ARG_CONFIG_NAME}"
        NO_CHECK_REQUIRED_COMPONENTS_MACRO
    )
    install(
        FILES
            "${CMAKE_CURRENT_BINARY_DIR}/${ARG_CONFIG_NAME}Config.cmake"
            "${ARG_VERSION_FILE}"
        DESTINATION "${native_cmakedir}/${ARG_CONFIG_NAME}"
        COMPONENT ${ARG_PACKAGE}
    )
    install(
        EXPORT ${ARG_EXPORT_NAME}
        NAMESPACE ${ARG_NAMESPACE}
        FILE "${ARG_EXPORT_FILE}"
        DESTINATION "${native_cmakedir}/${ARG_CONFIG_NAME}"
        COMPONENT ${ARG_PACKAGE}
    )
    if(ARG_CMAKE_SOURCE_DIRECTORY)
        install(DIRECTORY "${ARG_CMAKE_SOURCE_DIRECTORY}/"
            DESTINATION "${native_cmakedir}/${ARG_CONFIG_NAME}/cmake"
            COMPONENT ${ARG_PACKAGE}
        )
    endif()
endfunction()

# Stages a shipped yggdrasil cmake helper module into the consumer build tree
# and returns the copy's path in <out_var>. install(CODE) must include() these
# modules at install time, but ${yggdrasil_DIR} points into the (possibly
# transient) build venv's site-packages; the build tree is guaranteed to exist
# through the install step, so copying decouples install from that prefix.
function(_yggdrasil_stage_install_helper module_name out_var)
    set(staged_dir "${CMAKE_CURRENT_BINARY_DIR}/yggdrasil_install_helpers")
    configure_file("${yggdrasil_DIR}/${module_name}" "${staged_dir}/${module_name}" COPYONLY)
    set(${out_var} "${staged_dir}/${module_name}" PARENT_SCOPE)
endfunction()

# Wraps the shared runtime-path fixup for the wheel's native library dir.
function(yggdrasil_install_runtime_path_fixup PKG_UPPER package)
    _yggdrasil_stage_install_helper(yggdrasilFixRuntimePaths.cmake staged_module)
    install(CODE
        "include(\"${staged_module}\")
             yggdrasil_fix_runtime_paths(LIB_DIR_GLOB \"${${PKG_UPPER}_NATIVE_LIBDIR}\"
                                         RPATH \"${${PKG_UPPER}_INSTALL_NATIVE_LIBRARY_RPATH}\"
                                         RPATHS \"${${PKG_UPPER}_INSTALL_NATIVE_LIBRARY_RPATHS}\")"
        COMPONENT ${package}
    )
endfunction()

# Exports the provider library dirs to the install-time environment so that
# INSTALL_TIME stub generation can load the freshly installed module:
#   yggdrasil_install_provider_env(PYPDDL pypddl PREFIXES "${YGGDRASIL_NATIVE_PREFIX}" ...)
function(yggdrasil_install_provider_env PKG_UPPER package)
    cmake_parse_arguments(PARSE_ARGV 2 ARG "" "" "PREFIXES")
    set(install_library_paths)
    foreach(native_prefix IN LISTS ARG_PREFIXES)
        if(native_prefix)
            file(GLOB native_lib_dirs LIST_DIRECTORIES true "${native_prefix}/lib*")
            foreach(native_lib_dir IN LISTS native_lib_dirs)
                if(IS_DIRECTORY "${native_lib_dir}")
                    list(APPEND install_library_paths "${native_lib_dir}")
                endif()
            endforeach()
        endif()
    endforeach()
    list(JOIN install_library_paths ":" install_library_path)

    if(install_library_path)
        install(CODE
            "set(ENV{LD_LIBRARY_PATH} \"${install_library_path}:\$ENV{LD_LIBRARY_PATH}\")
             set(ENV{DYLD_LIBRARY_PATH} \"${install_library_path}:\$ENV{DYLD_LIBRARY_PATH}\")"
            COMPONENT ${package}
        )
    endif()
endfunction()

# Stub generation + install-time post-processing:
#   yggdrasil_install_python_stubs(PYPDDL pypddl MODULE pypddl._pypddl
#       RENAME_PACKAGES pypddl pyyggdrasil [PRIVATE_MODULE _pypddl])
function(yggdrasil_install_python_stubs PKG_UPPER package)
    cmake_parse_arguments(PARSE_ARGV 2 ARG "" "MODULE;PRIVATE_MODULE" "RENAME_PACKAGES")
    if(NOT ARG_MODULE)
        message(FATAL_ERROR "yggdrasil_install_python_stubs: MODULE is required")
    endif()

    nanobind_add_stub(
        ${package}_stubs
        MODULE ${ARG_MODULE}
        RECURSIVE
        INSTALL_TIME
        OUTPUT_PATH "${package}"
        PYTHON_PATH "."
        COMPONENT ${package}
    )

    set(private_module_argument)
    if(ARG_PRIVATE_MODULE)
        set(private_module_argument "PRIVATE_MODULE ${ARG_PRIVATE_MODULE}")
    endif()
    string(REPLACE ";" " " rename_packages "${ARG_RENAME_PACKAGES}")
    _yggdrasil_stage_install_helper(yggdrasilPatchPythonStubs.cmake staged_module)
    install(CODE
        "include(\"${staged_module}\")
             yggdrasil_patch_python_stubs(PACKAGE ${package} ${private_module_argument} RENAME_PACKAGES ${rename_packages})"
        COMPONENT ${package}
    )
endfunction()

# Installs the listed python source files preserving their relative layout:
#   yggdrasil_install_python_files(pypddl "${CMAKE_CURRENT_SOURCE_DIR}/src" FILES <rel-paths...>)
function(yggdrasil_install_python_files package source_dir)
    cmake_parse_arguments(PARSE_ARGV 2 ARG "" "" "FILES")
    foreach(python_file IN LISTS ARG_FILES)
        get_filename_component(python_file_directory "${python_file}" DIRECTORY)
        install(FILES "${source_dir}/${python_file}"
            DESTINATION "${python_file_directory}"
            COMPONENT ${package}
        )
    endforeach()
endfunction()
