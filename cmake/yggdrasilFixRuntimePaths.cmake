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
# On macOS the install name of every library is also rewritten to
# @rpath/<name>. Missing tools degrade to a warning, matching the historical
# per-repo scripts. Honors $ENV{DESTDIR}.

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

    file(GLOB native_lib_dirs LIST_DIRECTORIES true
        "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${ARG_LIB_DIR_GLOB}")
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

        set(native_libraries)
        foreach(native_lib_dir IN LISTS existing_lib_dirs)
            file(GLOB_RECURSE native_dir_libraries LIST_DIRECTORIES false
                "${native_lib_dir}/*.dylib"
                "${native_lib_dir}/*.dylib.*")
            list(APPEND native_libraries ${native_dir_libraries})
        endforeach()

        foreach(native_library IN LISTS native_libraries)
            get_filename_component(native_library_name "${native_library}" NAME)

            execute_process(
                COMMAND "${INSTALL_NAME_TOOL_EXECUTABLE}" -id "@rpath/${native_library_name}" "${native_library}"
                RESULT_VARIABLE install_name_result
                ERROR_VARIABLE install_name_error)
            if(NOT install_name_result EQUAL 0)
                message(WARNING "Could not update install name of ${native_library}: ${install_name_error}")
            endif()

            foreach(native_library_rpath IN LISTS ARG_RPATHS)
                execute_process(
                    COMMAND "${INSTALL_NAME_TOOL_EXECUTABLE}" -delete_rpath "${native_library_rpath}" "${native_library}"
                    OUTPUT_QUIET
                    ERROR_QUIET)
                execute_process(
                    COMMAND "${INSTALL_NAME_TOOL_EXECUTABLE}" -add_rpath "${native_library_rpath}" "${native_library}"
                    RESULT_VARIABLE rpath_result
                    ERROR_VARIABLE rpath_error)
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
            file(GLOB_RECURSE native_dir_libraries LIST_DIRECTORIES false
                "${native_lib_dir}/*.so"
                "${native_lib_dir}/*.so.*")
            list(APPEND native_libraries ${native_dir_libraries})
        endforeach()

        foreach(native_library IN LISTS native_libraries)
            execute_process(
                COMMAND "${PATCHELF_EXECUTABLE}" --set-rpath "${ARG_RPATH}" "${native_library}"
                RESULT_VARIABLE rpath_result
                ERROR_VARIABLE rpath_error)
            if(NOT rpath_result EQUAL 0)
                message(WARNING "Could not set rpath on ${native_library}: ${rpath_error}")
            endif()
        endforeach()
    endif()
endfunction()
