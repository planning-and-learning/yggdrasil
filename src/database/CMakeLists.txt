# The database engine, compiled once (docs/TEMPLATE_RULES.md, rules 8 and 18).

find_package(fmt CONFIG REQUIRED PATHS "${YGGDRASIL_NATIVE_PREFIX}" NO_DEFAULT_PATH)
find_package(TBB CONFIG REQUIRED PATHS "${YGGDRASIL_NATIVE_PREFIX}" NO_DEFAULT_PATH)

file(GLOB_RECURSE YGGDRASIL_DATABASE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

if(APPLE)
    set(YGGDRASIL_DATABASE_ORIGIN "@loader_path")
elseif(UNIX)
    set(YGGDRASIL_DATABASE_ORIGIN "$ORIGIN")
endif()

set(YGGDRASIL_DATABASE_UNITY_BATCH 8 CACHE STRING "Database library sources per compiled unit.")
# OFF lists each source in compile_commands.json, which clang-tidy (plumbcheck) needs.
option(YGGDRASIL_UNITY_BUILD "Compile the database library's sources in batches." ON)

function(yggdrasil_add_database_library target)
    add_library(${target} SHARED ${YGGDRASIL_DATABASE_SOURCES})
    target_compile_features(${target} PUBLIC cxx_std_20)
    # Lets calls inside the library inline and bind locally instead of going through the PLT.
    target_compile_options(${target} PRIVATE $<$<CXX_COMPILER_ID:GNU>:-fno-semantic-interposition>)
    target_include_directories(
        ${target}
        PUBLIC
            "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
            "$<BUILD_INTERFACE:${YGGDRASIL_NATIVE_PREFIX}/include>"
            "$<INSTALL_INTERFACE:pyyggdrasil/include>"
    )
    target_link_libraries(${target} PUBLIC fmt::fmt TBB::tbb)
    # Sources share the headers that dominate their cost; batches compile them once (docs/TEMPLATE_RULES.md, rule 11).
    set_target_properties(${target} PROPERTIES UNITY_BUILD ${YGGDRASIL_UNITY_BUILD} UNITY_BUILD_BATCH_SIZE ${YGGDRASIL_DATABASE_UNITY_BATCH})
    set_target_properties(
        ${target}
        PROPERTIES
            BUILD_RPATH "${YGGDRASIL_NATIVE_PREFIX}/${CMAKE_INSTALL_LIBDIR}"
            INSTALL_RPATH "${YGGDRASIL_DATABASE_ORIGIN}"
    )
endfunction()

yggdrasil_add_database_library(database)
add_library(yggdrasil::database ALIAS database)
set_target_properties(database PROPERTIES OUTPUT_NAME yggdrasil_database)

if(YGGDRASIL_BUILD_TESTS)
    # The work-counting tests need the counters in every translation unit that counts.
    yggdrasil_add_database_library(database_instrumented)
    target_compile_definitions(database_instrumented PUBLIC YGG_DISTANCE_INSTRUMENTATION=1 YGG_GENERIC_JOIN_INSTRUMENTATION=1)
endif()
