cmake_minimum_required(VERSION 3.21)

# brokkr (pybrokkr): build helpers, found through scikit-build-core in wheel builds, else asked of the Python that has it.
find_package(brokkr QUIET)
if(NOT brokkr_FOUND)
    if(Python_EXECUTABLE)
        set(_brokkr_python "${Python_EXECUTABLE}")
    else()
        find_program(_brokkr_python NAMES python3 python REQUIRED)
    endif()
    execute_process(COMMAND "${_brokkr_python}" -m pybrokkr --cmake-dir
                    OUTPUT_VARIABLE _brokkr_dir OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    set(brokkr_DIR "${_brokkr_dir}" CACHE PATH "The directory holding brokkrConfig.cmake" FORCE)
    find_package(brokkr REQUIRED)
endif()
brokkr_read_project_version(YGGDRASIL_PROJECT_VERSION "${CMAKE_CURRENT_SOURCE_DIR}/pyproject.toml")

project(yggdrasil VERSION "${YGGDRASIL_PROJECT_VERSION}")

# Project configuration

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

# Options

option(YGGDRASIL_USE_LLD "Use LLVM lld with Clang when available." ON)
option(YGGDRASIL_ENABLE_LTO "Enable link-time optimization for Release builds." ON)
option(YGGDRASIL_ENABLE_FMT_FORMATTERS "Enable Yggdrasil's public fmt::formatter specializations." ON)
option(YGGDRASIL_BUILD_TESTS "Build Yggdrasil C++ tests." OFF)
option(YGGDRASIL_BUILD_PROFILING "Build Yggdrasil profiling executables." OFF)
option(YGGDRASIL_STDLIB_ASSERTIONS "Check bounds and preconditions of the standard library in Debug builds." ON)

# Build configuration

# Debug builds check the standard library's preconditions, such as the bounds of operator[]
# on std::vector, std::array and std::span (libstdc++; libc++ in its hardened mode). The
# definitions apply to this project's own targets only and do not change the ABI.
if(YGGDRASIL_STDLIB_ASSERTIONS)
    add_compile_definitions(
        $<$<CONFIG:Debug>:_GLIBCXX_ASSERTIONS>
        "$<$<CONFIG:Debug>:_LIBCPP_HARDENING_MODE=(_LIBCPP_HARDENING_MODE_EXTENSIVE)>"
    )
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(
        CMAKE_BUILD_TYPE "Release" CACHE STRING
        "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE
    )
endif()

if(YGGDRASIL_ENABLE_FMT_FORMATTERS)
    add_compile_definitions(YGG_ENABLE_FMT_FORMATTERS=1)
else()
    add_compile_definitions(YGG_ENABLE_FMT_FORMATTERS=0)
endif()

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W1 /EHsc /bigobj /MP")
    string(APPEND CMAKE_EXE_LINKER_FLAGS " /IGNORE:4006,4044,4075")
else()
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    # Shadowed names hide the variable a statement meant; CI turns every warning into an error.
    add_compile_options(-Wall -Wextra -Wshadow)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # GCC 13+ flags views returned from temporary views; views never refer to the temporary.
        add_compile_options(-Wno-dangling-reference)
    endif()

    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Og -g2 -ggdb")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -g2 -fno-omit-frame-pointer -fno-lto")

    if(
        YGGDRASIL_USE_LLD
        AND (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    )
        find_program(LLD_LINKER NAMES ld.lld)
        if(LLD_LINKER)
            message(STATUS "Using lld: ${LLD_LINKER}")
            add_link_options(-fuse-ld=lld)
        else()
            message(STATUS "lld not found, falling back to default linker")
        endif()
    elseif(YGGDRASIL_USE_LLD AND YGGDRASIL_ENABLE_LTO AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(STATUS "Skipping lld with GNU because GCC LTO requires the GCC linker plugin")
    endif()

    if(YGGDRASIL_ENABLE_LTO AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto=auto")
        set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -flto=auto")
        set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} -flto=auto")
        set(CMAKE_MODULE_LINKER_FLAGS_RELEASE "${CMAKE_MODULE_LINKER_FLAGS_RELEASE} -flto=auto")
    elseif(
        YGGDRASIL_ENABLE_LTO
        AND APPLE
        AND (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    )
        # ld64's LTO drops weak explicit instantiations that nothing inside a dylib calls (template rule 9).
        message(STATUS "Skipping LTO on Apple: ld64 does not export unused explicit instantiations under LTO")
    elseif(
        YGGDRASIL_ENABLE_LTO
        AND (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    )
        # ThinLTO reduces release compile and link costs compared with full LTO.
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto=thin")
        if(APPLE)
            set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -Wl,-mllvm,-threads=2")
            set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} -Wl,-mllvm,-threads=2")
            set(CMAKE_MODULE_LINKER_FLAGS_RELEASE "${CMAKE_MODULE_LINKER_FLAGS_RELEASE} -Wl,-mllvm,-threads=2")
        else()
            set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -Wl,--thinlto-jobs=2")
            set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} -Wl,--thinlto-jobs=2")
            set(CMAKE_MODULE_LINKER_FLAGS_RELEASE "${CMAKE_MODULE_LINKER_FLAGS_RELEASE} -Wl,--thinlto-jobs=2")
        endif()
    endif()
endif()

if(CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Build configurations: ${CMAKE_CONFIGURATION_TYPES}")
else()
    message(STATUS "Build configuration: ${CMAKE_BUILD_TYPE}")
endif()

# Dependencies

# urd and the native libraries come from pyurd: its prefix (the build backend sets URD_NATIVE_PREFIX; a CMake build asks
# the Python that has pyurd installed).
brokkr_find_python_native_prefix(pyurd URD_NATIVE_PREFIX)
if(NOT URD_NATIVE_PREFIX OR NOT EXISTS "${URD_NATIVE_PREFIX}")
    message(FATAL_ERROR "pyurd's native prefix not found: install pyurd, or set URD_NATIVE_PREFIX")
endif()
list(PREPEND CMAKE_PREFIX_PATH "${URD_NATIVE_PREFIX}")
find_package(urd CONFIG REQUIRED PATHS "${URD_NATIVE_PREFIX}" NO_DEFAULT_PATH)

# Targets

add_subdirectory(python)

if(YGGDRASIL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(YGGDRASIL_BUILD_PROFILING)
    add_subdirectory(profiling)
endif()

add_custom_target(pyyggdrasil_wheel DEPENDS pyyggdrasil_python)
