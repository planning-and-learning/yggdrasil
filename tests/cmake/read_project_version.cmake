include("${CMAKE_CURRENT_LIST_DIR}/test_prelude.cmake")

include("${YGGDRASIL_PROJECT_ROOT}/cmake/read_project_version.cmake")

set(test_pyproject "${YGGDRASIL_TEST_BINARY_DIR}/read_project_version_pyproject.toml")
file(WRITE "${test_pyproject}"
    "[build-system]\n"
    "version = \"999.0\"\n"
    "\n"
    "[project]\n"
    "name = \"demo\"\n"
    "version = \"1.2.3\"\n"
    "\n"
    "[tool.example]\n"
    "version = \"888.0\"\n"
)

yggdrasil_read_project_version(project_version "${test_pyproject}")

if(NOT project_version STREQUAL "1.2.3")
    message(FATAL_ERROR "Expected project version 1.2.3, got ${project_version}")
endif()

set(single_quoted_pyproject "${YGGDRASIL_TEST_BINARY_DIR}/read_project_version_single_quoted_pyproject.toml")
file(WRITE "${single_quoted_pyproject}"
    "[project]\n"
    "name = 'demo'\n"
    "version = '2.0.1'\n"
)

yggdrasil_read_project_version(single_quoted_project_version "${single_quoted_pyproject}")

if(NOT single_quoted_project_version STREQUAL "2.0.1")
    message(FATAL_ERROR "Expected project version 2.0.1, got ${single_quoted_project_version}")
endif()

set(spaced_project_table "${YGGDRASIL_TEST_BINARY_DIR}/read_project_version_spaced_table_pyproject.toml")
file(WRITE "${spaced_project_table}"
    "[ project ]\n"
    "name = 'demo'\n"
    "version = '3.4.5' # inline comments are valid TOML\n"
)

yggdrasil_read_project_version(spaced_table_project_version "${spaced_project_table}")

if(NOT spaced_table_project_version STREQUAL "3.4.5")
    message(FATAL_ERROR "Expected project version 3.4.5, got ${spaced_table_project_version}")
endif()
