find_package(benchmark 1.8.3 REQUIRED PATHS "${YGGDRASIL_NATIVE_PREFIX}" NO_DEFAULT_PATH)
find_package(TBB CONFIG REQUIRED PATHS "${YGGDRASIL_NATIVE_PREFIX}" NO_DEFAULT_PATH)
find_package(Threads REQUIRED)

add_executable(formalism_repository "formalism/repository.cpp")
target_compile_features(formalism_repository PRIVATE cxx_std_20)
target_include_directories(
    formalism_repository
    PRIVATE
        "${PROJECT_SOURCE_DIR}/include"
        "${YGGDRASIL_NATIVE_PREFIX}/include"
)
target_link_libraries(formalism_repository PRIVATE benchmark::benchmark_main TBB::tbb Threads::Threads)
