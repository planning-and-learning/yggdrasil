# Helper functions for consumers of pip-installed provider packages
# (pyyggdrasil, pypddl, pytyr, ...). Shipped with the yggdrasil CMake package
# and included from yggdrasilConfig.cmake, so they become available after
# find_package(yggdrasil).
#
# State accumulated across calls (directory-scope variables):
#   YGGDRASIL_NATIVE_DEPENDENCY_INCLUDE_DIRECTORIES
#   YGGDRASIL_PYTHON_NATIVE_RUNTIME_PREFIXES
#   YGGDRASIL_PYTHON_NATIVE_RUNTIME_LIBDIRS

# Functions capture the policy state of their defining scope; set what they
# rely on explicitly so the helpers also work from cmake -P script mode.
cmake_policy(PUSH)
cmake_policy(SET CMP0057 NEW) # IN_LIST operator

# yggdrasil_find_python_native_prefix(<python_package> <out_prefix_var>)
#   - If <out_prefix_var> is already set (manual override), only ensures it is
#     on CMAKE_PREFIX_PATH.
#   - Otherwise queries the package for its cmake_prefix() (native_prefix() on
#     older releases) and prepends it to CMAKE_PREFIX_PATH.
function(yggdrasil_find_python_native_prefix python_package out_prefix_var)
  if(${out_prefix_var})
    if(NOT "${${out_prefix_var}}" IN_LIST CMAKE_PREFIX_PATH)
      list(PREPEND CMAKE_PREFIX_PATH "${${out_prefix_var}}")
      set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)
    endif()
    return()
  endif()

  if(NOT Python3_EXECUTABLE AND Python_EXECUTABLE)
    set(Python3_EXECUTABLE "${Python_EXECUTABLE}")
  endif()

  find_package(Python3 QUIET COMPONENTS Interpreter)
  if(NOT Python3_Interpreter_FOUND)
    return()
  endif()

  execute_process(
    COMMAND "${Python3_EXECUTABLE}" -c
            "import ${python_package} as m; print(getattr(m, 'cmake_prefix', m.native_prefix)())"
    RESULT_VARIABLE native_result
    OUTPUT_VARIABLE native_prefix
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )

  if(native_result EQUAL 0 AND EXISTS "${native_prefix}")
    list(PREPEND CMAKE_PREFIX_PATH "${native_prefix}")
    set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)
    set(${out_prefix_var} "${native_prefix}" PARENT_SCOPE)
    message(STATUS "Found ${python_package} native prefix: ${native_prefix}")
  endif()
endfunction()

function(yggdrasil_register_native_dependency_prefix native_prefix)
    if(NOT native_prefix)
        return()
    endif()

    set(native_include_dir_name "${CMAKE_INSTALL_INCLUDEDIR}")
    if(NOT native_include_dir_name)
        set(native_include_dir_name "include")
    endif()

    if(EXISTS "${native_prefix}/${native_include_dir_name}")
        list(APPEND YGGDRASIL_NATIVE_DEPENDENCY_INCLUDE_DIRECTORIES "${native_prefix}/${native_include_dir_name}")
        set(YGGDRASIL_NATIVE_DEPENDENCY_INCLUDE_DIRECTORIES "${YGGDRASIL_NATIVE_DEPENDENCY_INCLUDE_DIRECTORIES}" PARENT_SCOPE)
    endif()
endfunction()

# yggdrasil_register_python_native_runtime_prefix(<package_relative_prefix> [<abs_prefix>])
#   The optional absolute prefix is used to detect the library directory name
#   (lib vs lib64); without it the name defaults to CMAKE_INSTALL_LIBDIR.
function(yggdrasil_register_python_native_runtime_prefix package_relative_prefix)
    if(NOT package_relative_prefix)
        return()
    endif()

    set(native_lib_dir_name "${CMAKE_INSTALL_LIBDIR}")
    if(ARGC GREATER 1 AND ARGV1)
        foreach(candidate_lib_dir IN ITEMS "${ARGV1}/${CMAKE_INSTALL_LIBDIR}" "${ARGV1}/lib" "${ARGV1}/lib64")
            if(IS_DIRECTORY "${candidate_lib_dir}")
                get_filename_component(native_lib_dir_name "${candidate_lib_dir}" NAME)
                break()
            endif()
        endforeach()
    endif()

    list(APPEND YGGDRASIL_PYTHON_NATIVE_RUNTIME_PREFIXES "${package_relative_prefix}")
    list(APPEND YGGDRASIL_PYTHON_NATIVE_RUNTIME_LIBDIRS "${native_lib_dir_name}")
    set(YGGDRASIL_PYTHON_NATIVE_RUNTIME_PREFIXES "${YGGDRASIL_PYTHON_NATIVE_RUNTIME_PREFIXES}" PARENT_SCOPE)
    set(YGGDRASIL_PYTHON_NATIVE_RUNTIME_LIBDIRS "${YGGDRASIL_PYTHON_NATIVE_RUNTIME_LIBDIRS}" PARENT_SCOPE)
endfunction()

function(yggdrasil_make_python_native_runtime_rpaths output_variable origin package_relative_base)
    set(runtime_rpaths "${origin}")
    list(LENGTH YGGDRASIL_PYTHON_NATIVE_RUNTIME_PREFIXES runtime_prefix_count)
    if(runtime_prefix_count GREATER 0)
        math(EXPR runtime_prefix_last "${runtime_prefix_count} - 1")
        foreach(runtime_prefix_index RANGE ${runtime_prefix_last})
            list(GET YGGDRASIL_PYTHON_NATIVE_RUNTIME_PREFIXES ${runtime_prefix_index} package_relative_prefix)
            list(GET YGGDRASIL_PYTHON_NATIVE_RUNTIME_LIBDIRS ${runtime_prefix_index} native_lib_dir_name)
            list(APPEND runtime_rpaths "${origin}/${package_relative_base}${package_relative_prefix}/${native_lib_dir_name}")
        endforeach()
    endif()

    set("${output_variable}" "${runtime_rpaths}" PARENT_SCOPE)
endfunction()

function(yggdrasil_make_python_native_runtime_rpath_string output_variable origin package_relative_base)
    yggdrasil_make_python_native_runtime_rpaths(runtime_rpaths "${origin}" "${package_relative_base}")
    list(JOIN runtime_rpaths ":" runtime_rpath)
    set("${output_variable}" "${runtime_rpath}" PARENT_SCOPE)
endfunction()

cmake_policy(POP)
