# Dependencies

find_package(GTest "1.11.0" REQUIRED COMPONENTS GTest PATHS "${URD_NATIVE_PREFIX}" NO_DEFAULT_PATH)
find_package(Boost REQUIRED COMPONENTS json PATHS "${URD_NATIVE_PREFIX}" NO_DEFAULT_PATH)
find_package(fmt CONFIG REQUIRED PATHS "${URD_NATIVE_PREFIX}" NO_DEFAULT_PATH)
find_package(TBB CONFIG REQUIRED PATHS "${URD_NATIVE_PREFIX}" NO_DEFAULT_PATH)
find_package(Threads REQUIRED)

# Test helper

function(add_yggdrasil_gtest test_name source_file)
    add_executable(${test_name} ${source_file} main.cpp)
    target_compile_features(${test_name} PRIVATE cxx_std_20)
    target_include_directories(${test_name} PRIVATE
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(${test_name} SYSTEM PRIVATE
        "${URD_PREFIX}/include" "${URD_NATIVE_PREFIX}/include"
    )
    target_compile_definitions(${test_name} PRIVATE ROOT_DIR="${PROJECT_SOURCE_DIR}")
    target_link_libraries(${test_name} PRIVATE GTest::gtest Boost::json fmt::fmt TBB::tbb)
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES FAIL_REGULAR_EXPRESSION "does NOT link in any test case;Running 0 tests")
endfunction()

# Test sources mirror include/yggdrasil; target names use <dir>_<file>.

add_yggdrasil_gtest(extensions "extensions.cpp")

add_yggdrasil_gtest(containers_repository_types "containers/repository_types.cpp")

add_yggdrasil_gtest(formalism_row_key "formalism/row_key.cpp")
add_yggdrasil_gtest(formalism_repository_concurrent "formalism/repository_concurrent.cpp")
target_link_libraries(formalism_repository_concurrent PRIVATE Threads::Threads)

add_yggdrasil_gtest(core_closed_interval "core/closed_interval.cpp")
add_yggdrasil_gtest(core_core "core/core.cpp")
add_yggdrasil_gtest(core_family "core/family.cpp")
add_yggdrasil_gtest(core_portable_shuffle "core/portable_shuffle.cpp")

add_yggdrasil_gtest(diagnostics_diagnostic "diagnostics/diagnostic.cpp")
add_yggdrasil_gtest(diagnostics_x3 "diagnostics/x3.cpp")

add_yggdrasil_gtest(formatting_formatter "formatting/formatter.cpp")

add_yggdrasil_gtest(io_iostream "io/iostream.cpp")
add_yggdrasil_gtest(io_project_path "io/project_path.cpp")

add_yggdrasil_gtest(semantics_canonicalization "semantics/canonicalization.cpp")

add_yggdrasil_gtest(serialization_json "serialization/json.cpp")
add_yggdrasil_gtest(serialization_json_loader "serialization/json_loader.cpp")
add_yggdrasil_gtest(serialization_json_suite "serialization/json_suite.cpp")

add_library(serialization_dictionaries_library SHARED "serialization/dictionaries_library.cpp")
target_compile_features(serialization_dictionaries_library PRIVATE cxx_std_20)
target_include_directories(serialization_dictionaries_library PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}"
)
target_include_directories(serialization_dictionaries_library SYSTEM PRIVATE
    "${URD_PREFIX}/include" "${URD_NATIVE_PREFIX}/include"
)
target_link_libraries(serialization_dictionaries_library PRIVATE Boost::json fmt::fmt)
include(GenerateExportHeader)
generate_export_header(serialization_dictionaries_library)

add_yggdrasil_gtest(serialization_dictionaries "serialization/dictionaries.cpp")
target_include_directories(serialization_dictionaries PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(serialization_dictionaries PRIVATE serialization_dictionaries_library)
# Match separately built Python extension modules, whose native RTTI is hidden.
set_target_properties(serialization_dictionaries_library serialization_dictionaries PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
