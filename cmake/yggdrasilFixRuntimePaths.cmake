# Install-time runtime-path normalization for native libraries bundled in the
# provider wheels. Shipped with the yggdrasil CMake package; consumers include
# it from install(CODE ...) and call:
#
#   yggdrasil_fix_runtime_paths(
#       LIB_DIR_GLOB <glob>     # relative to the install prefix, may match
#                               # multiple directories (e.g. "pyyggdrasil/lib*")
#       [RPATH <string>]        # Linux: rpath set via patchelf (default $ORIGIN)
#       [RPATHS <list>]         # macOS: rpath entries re-added via
#                               # install_name_tool (default @loader_path)
#   )
#
# On macOS the install name of every library and absolute references to other
# bundled libraries are also rewritten to @rpath/<name>. Missing tools degrade
# to a warning, matching the historical per-repo scripts. Honors $ENV{DESTDIR}.

function(yggdrasil_fix_runtime_paths)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "LIB_DIR_GLOB;RPATH" "RPATHS")
    if(NOT ARG_LIB_DIR_GLOB)
        message(FATAL_ERROR "yggdrasil_fix_runtime_paths: LIB_DIR_GLOB is required")
    endif()
    if(NOT ARG_RPATH)
        set(ARG_RPATH "$ORIGIN")
    endif()
    if(NOT ARG_RPATHS)
        set(ARG_RPATHS "@loader_path")
    endif()

    file(GLOB native_lib_dirs
        LIST_DIRECTORIES true
        "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${ARG_LIB_DIR_GLOB}"
    )
    set(existing_lib_dirs)
    foreach(native_lib_dir IN LISTS native_lib_dirs)
        if(IS_DIRECTORY "${native_lib_dir}")
            list(APPEND existing_lib_dirs "${native_lib_dir}")
        endif()
    endforeach()
    if(NOT existing_lib_dirs)
        return()
    endif()

    if(CMAKE_HOST_APPLE)
        find_program(INSTALL_NAME_TOOL_EXECUTABLE install_name_tool)
        if(NOT INSTALL_NAME_TOOL_EXECUTABLE)
            message(WARNING "install_name_tool not found; native libraries keep their original runtime paths")
            return()
        endif()
        find_program(OTOOL_EXECUTABLE otool)
        if(NOT OTOOL_EXECUTABLE)
            message(WARNING "otool not found; bundled library dependencies keep their original paths")
        endif()

        set(native_libraries)
        foreach(native_lib_dir IN LISTS existing_lib_dirs)
            file(GLOB_RECURSE native_dir_libraries
                LIST_DIRECTORIES false
                "${native_lib_dir}/*.dylib"
                "${native_lib_dir}/*.dylib.*"
            )
            list(APPEND native_libraries ${native_dir_libraries})
        endforeach()
        list(REMOVE_DUPLICATES native_libraries)

        set(native_library_names)
        foreach(native_library IN LISTS native_libraries)
            get_filename_component(native_library_name "${native_library}" NAME)
            list(APPEND native_library_names "${native_library_name}")
        endforeach()
        list(REMOVE_DUPLICATES native_library_names)

        foreach(native_library IN LISTS native_libraries)
            get_filename_component(native_library_name "${native_library}" NAME)

            execute_process(
                COMMAND "${INSTALL_NAME_TOOL_EXECUTABLE}" -id "@rpath/${native_library_name}" "${native_library}"
                RESULT_VARIABLE install_name_result
                ERROR_VARIABLE install_name_error
            )
            if(NOT install_name_result EQUAL 0)
                message(WARNING "Could not update install name of ${native_library}: ${install_name_error}")
            endif()

            if(OTOOL_EXECUTABLE)
                execute_process(
                    COMMAND "${OTOOL_EXECUTABLE}" -L "${native_library}"
                    RESULT_VARIABLE otool_result
                    OUTPUT_VARIABLE otool_output
                    ERROR_VARIABLE otool_error
                )
                if(otool_result EQUAL 0)
                    string(REPLACE "\n" ";" otool_lines "${otool_output}")
                    set(native_dependencies)
                    foreach(otool_line IN LISTS otool_lines)
                        if(otool_line MATCHES "^[ \t]*(/.*) \\([^)]*\\)$")
                            list(APPEND native_dependencies "${CMAKE_MATCH_1}")
                        endif()
                    endforeach()
                    list(REMOVE_DUPLICATES native_dependencies)

                    foreach(native_dependency IN LISTS native_dependencies)
                        get_filename_component(native_dependency_name "${native_dependency}" NAME)
                        if(native_dependency_name IN_LIST native_library_names)
                            execute_process(
                                COMMAND
                                    "${INSTALL_NAME_TOOL_EXECUTABLE}"
                                    -change
                                    "${native_dependency}"
                                    "@rpath/${native_dependency_name}"
                                    "${native_library}"
                                RESULT_VARIABLE dependency_result
                                ERROR_VARIABLE dependency_error
                            )
                            if(NOT dependency_result EQUAL 0)
                                message(WARNING "Could not update dependency ${native_dependency} of ${native_library}: ${dependency_error}")
                            endif()
                        endif()
                    endforeach()
                else()
                    message(WARNING "Could not inspect dependencies of ${native_library}: ${otool_error}")
                endif()
            endif()

            foreach(native_library_rpath IN LISTS ARG_RPATHS)
                execute_process(
                    COMMAND
                        "${INSTALL_NAME_TOOL_EXECUTABLE}"
                        -delete_rpath
                        "${native_library_rpath}"
                        "${native_library}"
                    OUTPUT_QUIET
                    ERROR_QUIET
                )
                execute_process(
                    COMMAND "${INSTALL_NAME_TOOL_EXECUTABLE}" -add_rpath "${native_library_rpath}" "${native_library}"
                    RESULT_VARIABLE rpath_result
                    ERROR_VARIABLE rpath_error
                )
                if(NOT rpath_result EQUAL 0)
                    message(WARNING "Could not add ${native_library_rpath} rpath to ${native_library}: ${rpath_error}")
                endif()
            endforeach()
        endforeach()
    elseif(CMAKE_HOST_UNIX)
        find_program(PATCHELF_EXECUTABLE patchelf)
        if(NOT PATCHELF_EXECUTABLE)
            message(WARNING "patchelf not found; native libraries keep their original runtime paths")
            return()
        endif()

        set(native_libraries)
        foreach(native_lib_dir IN LISTS existing_lib_dirs)
            file(GLOB_RECURSE native_dir_libraries
                LIST_DIRECTORIES false
                "${native_lib_dir}/*.so"
                "${native_lib_dir}/*.so.*"
            )
            list(APPEND native_libraries ${native_dir_libraries})
        endforeach()

        foreach(native_library IN LISTS native_libraries)
            execute_process(
                COMMAND "${PATCHELF_EXECUTABLE}" --set-rpath "${ARG_RPATH}" "${native_library}"
                RESULT_VARIABLE rpath_result
                ERROR_VARIABLE rpath_error
            )
            if(NOT rpath_result EQUAL 0)
                message(WARNING "Could not set rpath on ${native_library}: ${rpath_error}")
            endif()
        endforeach()
    endif()
endfunction()
