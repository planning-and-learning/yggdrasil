enable_testing()

# Dependencies and targets

find_package(Python 3.9 REQUIRED COMPONENTS Interpreter Development.Module)
find_package(nanobind CONFIG REQUIRED PATHS "${YGGDRASIL_NATIVE_PREFIX}" NO_DEFAULT_PATH)
add_subdirectory(unit)

nanobind_add_module(yggdrasil_type_casters_test NB_SHARED python/type_casters.cpp)
target_compile_features(yggdrasil_type_casters_test PRIVATE cxx_std_20)
target_include_directories(yggdrasil_type_casters_test PRIVATE "${PROJECT_SOURCE_DIR}/include")

# Test registration

add_test(
    NAME python_type_casters
    COMMAND ${Python_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tests/python/type_casters.py
        $<TARGET_FILE:yggdrasil_type_casters_test>
)

add_test(
    NAME cmake_read_project_version
    COMMAND ${CMAKE_COMMAND}
        -DYGGDRASIL_PROJECT_ROOT=${PROJECT_SOURCE_DIR}
        -DYGGDRASIL_TEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${PROJECT_SOURCE_DIR}/tests/cmake/read_project_version.cmake
)

add_test(
    NAME cmake_patch_python_stubs
    COMMAND ${CMAKE_COMMAND}
        -DYGGDRASIL_PROJECT_ROOT=${PROJECT_SOURCE_DIR}
        -DYGGDRASIL_TEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${PROJECT_SOURCE_DIR}/tests/cmake/patch_python_stubs.cmake
)

add_test(
    NAME python_import_smoke
    COMMAND ${Python_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tests/python/import_smoke.py
        ${PROJECT_SOURCE_DIR}/python/src/pyyggdrasil/__init__.py
        $<TARGET_FILE:_pyyggdrasil>
)

add_test(
    NAME python_build_backend_smoke
    COMMAND ${Python_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tests/python/build_backend_smoke.py
)
