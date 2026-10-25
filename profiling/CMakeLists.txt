yggdrasil_source(YGGDRASIL_BENCHMARK_VERSION benchmark version)
find_package(benchmark ${YGGDRASIL_BENCHMARK_VERSION} REQUIRED PATHS "${YGGDRASIL_NATIVE_PREFIX}" NO_DEFAULT_PATH)
yggdrasil_source(YGGDRASIL_BOOST_VERSION boost version)
find_package(boost_heap ${YGGDRASIL_BOOST_VERSION} EXACT CONFIG REQUIRED PATHS "${YGGDRASIL_NATIVE_PREFIX}" NO_DEFAULT_PATH)
find_package(TBB CONFIG REQUIRED PATHS "${YGGDRASIL_NATIVE_PREFIX}" NO_DEFAULT_PATH)
find_package(Threads REQUIRED)

add_executable(tree_vector_set "containers/tree_vector_set.cpp")
target_compile_features(tree_vector_set PRIVATE cxx_std_20)
target_include_directories(
    tree_vector_set
    PRIVATE
        "${PROJECT_SOURCE_DIR}/include"
)
target_include_directories(
    tree_vector_set
    SYSTEM PRIVATE
        "${YGGDRASIL_NATIVE_PREFIX}/include"
)
target_link_libraries(tree_vector_set PRIVATE benchmark::benchmark_main Threads::Threads)

add_executable(formalism_repository "formalism/repository.cpp")
target_compile_features(formalism_repository PRIVATE cxx_std_20)
target_include_directories(
    formalism_repository
    PRIVATE
        "${PROJECT_SOURCE_DIR}/include"
)
target_include_directories(
    formalism_repository
    SYSTEM PRIVATE
        "${YGGDRASIL_NATIVE_PREFIX}/include"
)
target_link_libraries(formalism_repository PRIVATE benchmark::benchmark_main TBB::tbb Threads::Threads)

add_executable(database_incremental_benchmark "database/incremental.cpp")
target_compile_features(database_incremental_benchmark PRIVATE cxx_std_20)
target_include_directories(
    database_incremental_benchmark
    PRIVATE
        "${PROJECT_SOURCE_DIR}/include"
)
target_include_directories(
    database_incremental_benchmark
    SYSTEM PRIVATE
        "${YGGDRASIL_NATIVE_PREFIX}/include"
)
target_link_libraries(database_incremental_benchmark PRIVATE yggdrasil::database benchmark::benchmark_main Threads::Threads)

add_executable(database_distance_benchmark "database/distance.cpp")
target_compile_features(database_distance_benchmark PRIVATE cxx_std_20)
target_include_directories(
    database_distance_benchmark
    PRIVATE
        "${PROJECT_SOURCE_DIR}/include"
)
target_include_directories(
    database_distance_benchmark
    SYSTEM PRIVATE
        "${YGGDRASIL_NATIVE_PREFIX}/include"
)
target_link_libraries(database_distance_benchmark PRIVATE yggdrasil::database benchmark::benchmark_main Boost::heap Threads::Threads)

add_executable(database_query_benchmark "database/query.cpp")
target_compile_features(database_query_benchmark PRIVATE cxx_std_20)
target_include_directories(database_query_benchmark PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)
target_include_directories(database_query_benchmark SYSTEM PRIVATE
    "${YGGDRASIL_NATIVE_PREFIX}/include"
)
target_link_libraries(database_query_benchmark PRIVATE yggdrasil::database benchmark::benchmark_main Boost::heap Threads::Threads)

# A plain executable, optimized even in Debug builds so its timings mean something.
add_executable(must_pass_sokoban "database/must_pass_sokoban.cpp")
target_link_libraries(must_pass_sokoban PRIVATE yggdrasil::database)
target_compile_features(must_pass_sokoban PRIVATE cxx_std_20)
target_compile_options(must_pass_sokoban PRIVATE -O2 -DNDEBUG)
target_include_directories(must_pass_sokoban PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)
target_include_directories(must_pass_sokoban SYSTEM PRIVATE
    "${YGGDRASIL_NATIVE_PREFIX}/include"
)
