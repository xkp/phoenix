cmake_minimum_required(VERSION 3.20)

project(phoenix_cgal_app VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CGAL REQUIRED)

add_executable(phoenix_cgal_app
    src/cache.cpp
    src/execution.cpp
    src/geometry.cpp
    src/graph.cpp
    src/graph_validation.cpp
    src/labels.cpp
    src/main.cpp
    src/publication.cpp
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
    src/geometry.cpp
    src/labels.cpp
    src/values.cpp
    tests/value_tests.cpp
)

add_executable(phoenix_label_tests
    src/graph.cpp
    src/labels.cpp
    tests/label_tests.cpp
)

target_include_directories(phoenix_label_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(MSVC)
    target_compile_options(phoenix_label_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(phoenix_label_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(phoenix_geometry_tests
    src/geometry.cpp
    src/labels.cpp
    src/graph.cpp
    tests/geometry_tests.cpp
)

target_include_directories(phoenix_geometry_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(MSVC)
    target_compile_options(phoenix_geometry_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(phoenix_geometry_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(phoenix_working_geometry_tests
    src/geometry.cpp
    src/graph.cpp
    src/labels.cpp
    src/working_geometry.cpp
    tests/working_geometry_tests.cpp
)

target_include_directories(phoenix_working_geometry_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(phoenix_working_geometry_tests PRIVATE CGAL::CGAL)

if(MSVC)
    target_compile_options(phoenix_working_geometry_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(phoenix_working_geometry_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(phoenix_publication_tests
    src/geometry.cpp
    src/graph.cpp
    src/labels.cpp
    src/publication.cpp
    tests/publication_tests.cpp
)

target_include_directories(phoenix_publication_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(MSVC)
    target_compile_options(phoenix_publication_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(phoenix_publication_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(phoenix_extrusion_plan_builder_tests
    src/extrusion/plan_builder.cpp
    tests/extrusion_plan_builder_tests.cpp
)

target_include_directories(phoenix_extrusion_plan_builder_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(phoenix_extrusion_plan_builder_tests PRIVATE CGAL::CGAL)

if(MSVC)
    target_compile_options(phoenix_extrusion_plan_builder_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(phoenix_extrusion_plan_builder_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(phoenix_extrusion_profile_tests
    src/extrusion/profile.cpp
    src/geometry.cpp
    src/graph.cpp
    src/labels.cpp
    tests/extrusion_profile_tests.cpp
)

target_include_directories(phoenix_extrusion_profile_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(phoenix_extrusion_profile_tests PRIVATE CGAL::CGAL)

if(MSVC)
    target_compile_options(phoenix_extrusion_profile_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(phoenix_extrusion_profile_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(phoenix_extrusion_output_builder_tests
    src/geometry.cpp
    src/graph.cpp
    src/labels.cpp
    src/working_geometry.cpp
    src/working_geometry_builder.cpp
    tests/extrusion_output_builder_tests.cpp
)

target_include_directories(phoenix_extrusion_output_builder_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(phoenix_extrusion_output_builder_tests PRIVATE CGAL::CGAL)

if(MSVC)
    target_compile_options(phoenix_extrusion_output_builder_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(phoenix_extrusion_output_builder_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(phoenix_extrusion_kernel_compile_tests
    src/extrusion/kernel_port.cpp
    src/extrusion/plan_builder.cpp
    src/extrusion/profile.cpp
    src/geometry.cpp
    src/graph.cpp
    src/labels.cpp
    src/working_geometry.cpp
    src/working_geometry_builder.cpp
    tests/extrusion_kernel_compile_tests.cpp
)

target_include_directories(phoenix_extrusion_kernel_compile_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(phoenix_extrusion_kernel_compile_tests PRIVATE CGAL::CGAL)

if(MSVC)
    target_compile_options(phoenix_extrusion_kernel_compile_tests PRIVATE /W4 /permissive- /bigobj)
else()
    target_compile_options(phoenix_extrusion_kernel_compile_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(phoenix_extrusion_instruction_tests
    src/cache.cpp
    src/execution.cpp
    src/extrusion/instruction.cpp
    src/extrusion/kernel_port.cpp
    src/extrusion/plan_builder.cpp
    src/extrusion/profile.cpp
    src/geometry.cpp
    src/graph.cpp
    src/invalidation.cpp
    src/labels.cpp
    src/partial_run.cpp
    src/partial_run_apply.cpp
    src/partial_run_scope.cpp
    src/publication.cpp
    src/randomness.cpp
    src/scene_update.cpp
    src/values.cpp
    src/working_geometry.cpp
    src/working_geometry_builder.cpp
    tests/extrusion_instruction_tests.cpp
)

target_include_directories(phoenix_extrusion_instruction_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(phoenix_extrusion_instruction_tests PRIVATE CGAL::CGAL)

if(MSVC)
    target_compile_options(phoenix_extrusion_instruction_tests PRIVATE /W4 /permissive- /bigobj)
else()
    target_compile_options(phoenix_extrusion_instruction_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

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
    src/cache.cpp
    src/execution.cpp
    src/publication.cpp
    src/graph.cpp
    src/geometry.cpp
    src/labels.cpp
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
    src/geometry.cpp
    src/labels.cpp
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
    src/geometry.cpp
    src/labels.cpp
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
    src/geometry.cpp
    src/labels.cpp
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
    src/publication.cpp
    src/graph.cpp
    src/geometry.cpp
    src/labels.cpp
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
    src/publication.cpp
    src/graph.cpp
    src/geometry.cpp
    src/labels.cpp
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
    src/cache.cpp
    src/execution.cpp
    src/publication.cpp
    src/graph.cpp
    src/geometry.cpp
    src/labels.cpp
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
    src/publication.cpp
    src/graph.cpp
    src/geometry.cpp
    src/labels.cpp
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
    src/cache.cpp
    src/execution.cpp
    src/publication.cpp
    src/graph.cpp
    src/geometry.cpp
    src/labels.cpp
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
    src/publication.cpp
    src/graph.cpp
    src/geometry.cpp
    src/labels.cpp
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
