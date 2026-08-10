cmake_minimum_required(VERSION 3.20)

project(phoenix_cgal_app VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CGAL REQUIRED)

add_executable(phoenix_cgal_app
    src/execution.cpp
    src/graph.cpp
    src/graph_validation.cpp
    src/main.cpp
    src/randomness.cpp
    src/values.cpp
)

target_include_directories(phoenix_cgal_app
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(phoenix_cgal_app
    PRIVATE
        CGAL::CGAL
)

if(MSVC)
    target_compile_options(phoenix_cgal_app PRIVATE /W4 /permissive-)
else()
    target_compile_options(phoenix_cgal_app PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(phoenix_graph_validation_tests
    src/graph.cpp
    src/graph_validation.cpp
    tests/graph_validation_tests.cpp
)

target_include_directories(phoenix_graph_validation_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(phoenix_graph_validation_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(phoenix_graph_validation_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(phoenix_value_tests
    src/values.cpp
    tests/value_tests.cpp
)

target_include_directories(phoenix_value_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(phoenix_value_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(phoenix_value_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(phoenix_execution_tests
    src/execution.cpp
    src/graph.cpp
    src/randomness.cpp
    src/values.cpp
    tests/execution_tests.cpp
)

target_include_directories(phoenix_execution_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(phoenix_execution_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(phoenix_execution_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(phoenix_invalidation_tests
    src/graph.cpp
    src/invalidation.cpp
    tests/invalidation_tests.cpp
)

target_include_directories(phoenix_invalidation_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(phoenix_invalidation_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(phoenix_invalidation_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(phoenix_cache_tests
    src/cache.cpp
    src/values.cpp
    tests/cache_tests.cpp
)

target_include_directories(phoenix_cache_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(phoenix_cache_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(phoenix_cache_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(phoenix_partial_run_tests
    src/cache.cpp
    src/graph.cpp
    src/invalidation.cpp
    src/partial_run.cpp
    src/values.cpp
    tests/partial_run_tests.cpp
)

target_include_directories(phoenix_partial_run_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(phoenix_partial_run_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(phoenix_partial_run_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(phoenix_scene_update_tests
    src/scene_update.cpp
    src/values.cpp
    tests/scene_update_tests.cpp
)

target_include_directories(phoenix_scene_update_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(phoenix_scene_update_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(phoenix_scene_update_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(phoenix_partial_run_apply_tests
    src/cache.cpp
    src/execution.cpp
    src/graph.cpp
    src/partial_run_apply.cpp
    src/randomness.cpp
    src/scene_update.cpp
    src/values.cpp
    tests/partial_run_apply_tests.cpp
)

target_include_directories(phoenix_partial_run_apply_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(phoenix_partial_run_apply_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(phoenix_partial_run_apply_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(phoenix_partial_run_scope_tests
    src/cache.cpp
    src/execution.cpp
    src/graph.cpp
    src/partial_run_apply.cpp
    src/partial_run_scope.cpp
    src/randomness.cpp
    src/scene_update.cpp
    src/values.cpp
    tests/partial_run_scope_tests.cpp
)

target_include_directories(phoenix_partial_run_scope_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(phoenix_partial_run_scope_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(phoenix_partial_run_scope_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(phoenix_partial_run_scope_index_tests
    src/execution.cpp
    src/graph.cpp
    src/partial_run_scope_index.cpp
    src/randomness.cpp
    src/values.cpp
    tests/partial_run_scope_index_tests.cpp
)

target_include_directories(phoenix_partial_run_scope_index_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(phoenix_partial_run_scope_index_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(phoenix_partial_run_scope_index_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(phoenix_partial_run_scope_discovery_tests
    src/cache.cpp
    src/execution.cpp
    src/graph.cpp
    src/invalidation.cpp
    src/partial_run.cpp
    src/partial_run_scope.cpp
    src/partial_run_scope_discovery.cpp
    src/partial_run_scope_index.cpp
    src/randomness.cpp
    src/values.cpp
    tests/partial_run_scope_discovery_tests.cpp
)

target_include_directories(phoenix_partial_run_scope_discovery_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(phoenix_partial_run_scope_discovery_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(phoenix_partial_run_scope_discovery_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(phoenix_partial_run_instruction_index_tests
    src/execution.cpp
    src/graph.cpp
    src/partial_run_instruction_index.cpp
    src/partial_run_scope_index.cpp
    src/randomness.cpp
    src/values.cpp
    tests/partial_run_instruction_index_tests.cpp
)

target_include_directories(phoenix_partial_run_instruction_index_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(phoenix_partial_run_instruction_index_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(phoenix_partial_run_instruction_index_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(phoenix_partial_run_dirty_instruction_discovery_tests
    src/cache.cpp
    src/execution.cpp
    src/graph.cpp
    src/invalidation.cpp
    src/partial_run.cpp
    src/partial_run_dirty_instruction_discovery.cpp
    src/partial_run_instruction_index.cpp
    src/partial_run_scope.cpp
    src/partial_run_scope_discovery.cpp
    src/partial_run_scope_index.cpp
    src/randomness.cpp
    src/values.cpp
    tests/partial_run_dirty_instruction_discovery_tests.cpp
)

target_include_directories(phoenix_partial_run_dirty_instruction_discovery_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(phoenix_partial_run_dirty_instruction_discovery_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(phoenix_partial_run_dirty_instruction_discovery_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
