# Wrapper around the verbatim upstream nanobind config, which is installed to
# the ./cmake/ subdirectory. Inside the upstream config NB_DIR resolves to this
# directory, so its NB_DIR-relative lookups (cmake/darwin-ld-*.sym,
# cmake/darwin-python-path.py, stubgen.py) work by placement alone.
#
# After loading the upstream config (which verifies that Python was found),
# define an IMPORTED `nanobind` target pointing at the prebuilt shared runtime:
# nanobind_build_library() early-returns when the target already exists, so
# nanobind_add_module(... NB_SHARED) links the shipped library instead of
# rebuilding nanobind from source. The nanobind sources are not shipped;
# static builds (nanobind_add_module without NB_SHARED) are unsupported and
# fail at configure time with missing-source errors.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/nanobind-config.cmake")

if(NOT TARGET nanobind)
  get_filename_component(_YGGDRASIL_NANOBIND_LIBDIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
  get_filename_component(_YGGDRASIL_NANOBIND_PREFIX "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

  if(WIN32)
    set(_YGGDRASIL_NANOBIND_LIBRARY "${_YGGDRASIL_NANOBIND_PREFIX}/bin/nanobind.dll")
  elseif(APPLE)
    set(_YGGDRASIL_NANOBIND_LIBRARY "${_YGGDRASIL_NANOBIND_LIBDIR}/libnanobind.dylib")
  else()
    set(_YGGDRASIL_NANOBIND_LIBRARY "${_YGGDRASIL_NANOBIND_LIBDIR}/libnanobind.so")
  endif()

  if(NOT EXISTS "${_YGGDRASIL_NANOBIND_LIBRARY}")
    message(FATAL_ERROR "nanobind shared runtime library not found: ${_YGGDRASIL_NANOBIND_LIBRARY}")
  endif()

  add_library(nanobind SHARED IMPORTED)
  set_target_properties(nanobind PROPERTIES
    IMPORTED_LOCATION "${_YGGDRASIL_NANOBIND_LIBRARY}"
    INTERFACE_COMPILE_DEFINITIONS NB_SHARED
    INTERFACE_INCLUDE_DIRECTORIES "${Python_INCLUDE_DIRS};${_YGGDRASIL_NANOBIND_PREFIX}/include"
    INTERFACE_COMPILE_FEATURES cxx_std_17)

  if(WIN32)
    set_target_properties(nanobind PROPERTIES
      IMPORTED_IMPLIB "${_YGGDRASIL_NANOBIND_LIBDIR}/nanobind.lib"
      INTERFACE_LINK_LIBRARIES Python::Module)
  endif()

  unset(_YGGDRASIL_NANOBIND_LIBRARY)
  unset(_YGGDRASIL_NANOBIND_LIBDIR)
  unset(_YGGDRASIL_NANOBIND_PREFIX)
endif()
