# Dependencies

find_package(GTest "1.11.0" REQUIRED COMPONENTS GTest PATHS "${YGGDRASIL_NATIVE_PREFIX}" NO_DEFAULT_PATH)
find_package(Boost REQUIRED COMPONENTS json PATHS "${YGGDRASIL_NATIVE_PREFIX}" NO_DEFAULT_PATH)
find_package(fmt CONFIG REQUIRED PATHS "${YGGDRASIL_NATIVE_PREFIX}" NO_DEFAULT_PATH)
find_package(TBB CONFIG REQUIRED PATHS "${YGGDRASIL_NATIVE_PREFIX}" NO_DEFAULT_PATH)

# Test helper

function(add_yggdrasil_gtest test_name source_file)
    add_executable(${test_name} ${source_file} main.cpp)
    target_compile_features(${test_name} PRIVATE cxx_std_20)
    target_include_directories(${test_name} PRIVATE
        "${PROJECT_SOURCE_DIR}/include"
        "${YGGDRASIL_NATIVE_PREFIX}/include"
    )
    target_compile_definitions(${test_name} PRIVATE ROOT_DIR="${PROJECT_SOURCE_DIR}")
    target_link_libraries(${test_name} PRIVATE GTest::gtest Boost::json fmt::fmt TBB::tbb)
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

# Test sources mirror include/yggdrasil; target names use <dir>_<file>.

add_yggdrasil_gtest(extensions "extensions.cpp")

add_yggdrasil_gtest(buffer_indexed_hash_set "buffer/indexed_hash_set.cpp")
add_yggdrasil_gtest(buffer_segmented_buffer "buffer/segmented_buffer.cpp")

add_yggdrasil_gtest(containers_associative_containers "containers/associative_containers.cpp")
add_yggdrasil_gtest(containers_bit_packed_array_pool "containers/bit_packed_array_pool.cpp")
add_yggdrasil_gtest(containers_bit_packed_array_set "containers/bit_packed_array_set.cpp")
add_yggdrasil_gtest(containers_block_array_set "containers/block_array_set.cpp")
add_yggdrasil_gtest(containers_containers "containers/containers.cpp")
add_yggdrasil_gtest(containers_dynamic_bitset "containers/dynamic_bitset.cpp")
add_yggdrasil_gtest(containers_indexed_hash_set "containers/indexed_hash_set.cpp")
add_yggdrasil_gtest(containers_raw_array_pool "containers/raw_array_pool.cpp")
add_yggdrasil_gtest(containers_raw_array_set "containers/raw_array_set.cpp")
add_yggdrasil_gtest(containers_raw_vector_pool "containers/raw_vector_pool.cpp")
add_yggdrasil_gtest(containers_raw_vector_set "containers/raw_vector_set.cpp")
add_yggdrasil_gtest(containers_repository_types "containers/repository_types.cpp")
add_yggdrasil_gtest(containers_segmented_vector "containers/segmented_vector.cpp")
add_yggdrasil_gtest(containers_shared_object_pool "containers/shared_object_pool.cpp")
add_yggdrasil_gtest(containers_unique_object_pool "containers/unique_object_pool.cpp")
add_yggdrasil_gtest(containers_vector "containers/vector.cpp")

add_yggdrasil_gtest(core_closed_interval "core/closed_interval.cpp")
add_yggdrasil_gtest(core_concepts "core/concepts.cpp")
add_yggdrasil_gtest(core_core "core/core.cpp")
add_yggdrasil_gtest(core_path "core/path.cpp")

add_yggdrasil_gtest(formatting_formatter "formatting/formatter.cpp")

add_yggdrasil_gtest(io_iostream "io/iostream.cpp")
add_yggdrasil_gtest(io_project_adapters "io/project_adapters.cpp")
add_yggdrasil_gtest(io_project_path "io/project_path.cpp")

add_yggdrasil_gtest(semantics_canonicalization "semantics/canonicalization.cpp")
add_yggdrasil_gtest(semantics_comparison "semantics/comparison.cpp")
add_yggdrasil_gtest(semantics_comparators "semantics/comparators.cpp")
add_yggdrasil_gtest(semantics_equal_to "semantics/equal_to.cpp")
add_yggdrasil_gtest(semantics_hash "semantics/hash.cpp")

add_yggdrasil_gtest(serialization_json "serialization/json.cpp")
add_yggdrasil_gtest(serialization_json_loader "serialization/json_loader.cpp")
add_yggdrasil_gtest(serialization_json_suite "serialization/json_suite.cpp")
