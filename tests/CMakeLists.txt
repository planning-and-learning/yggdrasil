enable_testing()

# Dependencies and targets

find_package(Python 3.11 REQUIRED COMPONENTS Interpreter Development.Module)
find_package(nanobind CONFIG REQUIRED PATHS ${CMAKE_PREFIX_PATH} NO_DEFAULT_PATH)
add_subdirectory(unit)

nanobind_add_module(yggdrasil_type_casters_test NB_SUPPRESS_WARNINGS BACKEND_MODULE nanobind_backend integration/python/type_casters.cpp integration/python/type_casters_module.cpp)
target_compile_features(yggdrasil_type_casters_test PRIVATE cxx_std_20)
target_include_directories(yggdrasil_type_casters_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
# The dependencies' headers are system headers, so the build's warnings cover only this project.
target_include_directories(yggdrasil_type_casters_test SYSTEM PRIVATE "${URD_PREFIX}/include" "${URD_NATIVE_PREFIX}/include")

nanobind_add_module(yggdrasil_diagnostics_test NB_SUPPRESS_WARNINGS BACKEND_MODULE nanobind_backend integration/python/diagnostics.cpp integration/python/diagnostics_module.cpp)
target_compile_features(yggdrasil_diagnostics_test PRIVATE cxx_std_20)
target_include_directories(yggdrasil_diagnostics_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
# The dependencies' headers are system headers, so the build's warnings cover only this project.
target_include_directories(yggdrasil_diagnostics_test SYSTEM PRIVATE "${URD_PREFIX}/include" "${URD_NATIVE_PREFIX}/include")

# Test registration

add_test(
    NAME python_type_casters
    COMMAND ${Python_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tests/integration/python/type_casters.py
        $<TARGET_FILE:yggdrasil_type_casters_test>
)

add_test(
    NAME python_import_smoke
    COMMAND ${Python_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tests/integration/python/import_smoke.py
        ${PROJECT_SOURCE_DIR}/python/src/pyyggdrasil/__init__.py
        $<TARGET_FILE:_pyyggdrasil>
)

add_test(
    NAME python_diagnostics
    COMMAND ${Python_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tests/integration/python/diagnostics.py
        ${PROJECT_SOURCE_DIR}/python/src/pyyggdrasil/__init__.py
        $<TARGET_FILE:_pyyggdrasil>
        $<TARGET_FILE:yggdrasil_diagnostics_test>
)

