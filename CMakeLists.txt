cmake_minimum_required(VERSION 3.21)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/read_project_version.cmake")
yggdrasil_read_project_version(YGGDRASIL_PROJECT_VERSION "${CMAKE_CURRENT_SOURCE_DIR}/pyproject.toml")

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

# Build configuration

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
    add_compile_options(-Wall -Wextra)

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
        # Cap release LTO parallelism to reduce RAM spikes.
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto=2")
        set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -flto=2")
        set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} -flto=2")
        set(CMAKE_MODULE_LINKER_FLAGS_RELEASE "${CMAKE_MODULE_LINKER_FLAGS_RELEASE} -flto=2")
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

set(
    YGGDRASIL_NATIVE_PREFIX
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencies-install"
    CACHE PATH
    "Native dependency prefix to package."
)

if(NOT EXISTS "${YGGDRASIL_NATIVE_PREFIX}")
    message(FATAL_ERROR "Native prefix does not exist: ${YGGDRASIL_NATIVE_PREFIX}")
endif()

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
