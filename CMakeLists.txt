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

add_library(phoenix_partition_production_probe OBJECT EXCLUDE_FROM_ALL
        src/partition/ported/production/partition_solver.cpp
        src/partition/ported/production/partition_solver_constraints.cpp
        src/partition/ported/production/partition_tesselator.cpp
        src/partition/ported/production/partition_errors.cpp
    )
    target_include_directories(phoenix_partition_production_probe PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/phoenix/partition/ported/production"
    )
    target_link_libraries(phoenix_partition_production_probe PRIVATE CGAL::CGAL)
    if(MSVC)
        target_compile_options(phoenix_partition_production_probe PRIVATE
            /W4 /permissive /Zc:strictStrings- /Zc:twoPhase- /bigobj
            "/FI${CMAKE_CURRENT_SOURCE_DIR}/include/phoenix/partition/ported/production_probe_compat.hpp")
    else()
        target_compile_options(phoenix_partition_production_probe PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()

add_executable(phoenix_partition_production_adapter_tests
        src/geometry.cpp
        src/labels.cpp
        src/publication.cpp
        src/working_geometry.cpp
        src/working_geometry_builder.cpp
        src/partition/working_face.cpp
        src/partition/ported/compatibility_runtime.cpp
        src/partition/production_adapter.cpp
        tests/partition_production_adapter_tests.cpp
        $<TARGET_OBJECTS:phoenix_partition_production_probe>
    )
    target_include_directories(phoenix_partition_production_adapter_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/phoenix/partition/ported/production"
    )
    target_link_libraries(phoenix_partition_production_adapter_tests PRIVATE CGAL::CGAL)
    if(MSVC)
        target_compile_options(phoenix_partition_production_adapter_tests PRIVATE
            /W4 /permissive /Zc:strictStrings- /Zc:twoPhase- /bigobj
            "/FI${CMAKE_CURRENT_SOURCE_DIR}/include/phoenix/partition/ported/production_probe_compat.hpp")
    else()
        target_compile_options(phoenix_partition_production_adapter_tests PRIVATE
            -Wall -Wextra -Wpedantic)
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

add_library(phoenix_inset_production_oracle OBJECT EXCLUDE_FROM_ALL
    src/inset/ported/production/inset.cpp
)
target_include_directories(phoenix_inset_production_oracle PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(phoenix_inset_production_oracle PRIVATE CGAL::CGAL)
if(MSVC)
    target_compile_options(phoenix_inset_production_oracle PRIVATE
        /W4 /permissive /Zc:strictStrings- /Zc:twoPhase- /bigobj
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/include/phoenix/partition/ported/production_probe_compat.hpp")
else()
    target_compile_options(phoenix_inset_production_oracle PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

add_executable(phoenix_merge_production_oracle_tests
    tests/merge_production_oracle_tests.cpp
)
target_include_directories(phoenix_merge_production_oracle_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(phoenix_merge_production_oracle_tests PRIVATE CGAL::CGAL)
if(MSVC)
    target_compile_options(phoenix_merge_production_oracle_tests PRIVATE
        /W4 /permissive /Zc:strictStrings- /Zc:twoPhase- /bigobj
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/include/phoenix/partition/ported/production_probe_compat.hpp")
else()
    target_compile_options(phoenix_merge_production_oracle_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

add_executable(phoenix_merge_production_adapter_tests
    src/cache.cpp
    src/execution.cpp
    src/geometry.cpp
    src/graph.cpp
    src/invalidation.cpp
    src/labels.cpp
    src/partial_run.cpp
    src/partial_run_apply.cpp
    src/partial_run_scope.cpp
    src/randomness.cpp
    src/scene_update.cpp
    src/values.cpp
    src/publication.cpp
    src/working_geometry.cpp
    src/working_geometry_builder.cpp
    src/merge/production_adapter.cpp
    src/merge/instruction.cpp
    tests/merge_production_adapter_tests.cpp
)
target_include_directories(phoenix_merge_production_adapter_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(phoenix_merge_production_adapter_tests PRIVATE CGAL::CGAL)
if(MSVC)
    target_compile_options(phoenix_merge_production_adapter_tests PRIVATE
        /W4 /permissive /Zc:strictStrings- /Zc:twoPhase- /bigobj
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/include/phoenix/partition/ported/production_probe_compat.hpp")
else()
    target_compile_options(phoenix_merge_production_adapter_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

add_executable(phoenix_inset_production_oracle_tests
    tests/inset_production_oracle_tests.cpp
    $<TARGET_OBJECTS:phoenix_inset_production_oracle>
)
target_include_directories(phoenix_inset_production_oracle_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(phoenix_inset_production_oracle_tests PRIVATE CGAL::CGAL)
if(MSVC)
    target_compile_options(phoenix_inset_production_oracle_tests PRIVATE
        /W4 /permissive /Zc:strictStrings- /Zc:twoPhase- /bigobj
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/include/phoenix/partition/ported/production_probe_compat.hpp")
else()
    target_compile_options(phoenix_inset_production_oracle_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

add_executable(phoenix_inset_production_adapter_tests
    src/cache.cpp
    src/execution.cpp
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
    src/working_geometry.cpp
    src/working_geometry_builder.cpp
    src/values.cpp
    src/partition/working_face.cpp
    src/inset/instruction.cpp
    src/inset/production_adapter.cpp
    tests/inset_production_adapter_tests.cpp
    $<TARGET_OBJECTS:phoenix_inset_production_oracle>
)
target_include_directories(phoenix_inset_production_adapter_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(phoenix_inset_production_adapter_tests PRIVATE CGAL::CGAL)
if(MSVC)
    target_compile_options(phoenix_inset_production_adapter_tests PRIVATE
        /W4 /permissive /Zc:strictStrings- /Zc:twoPhase- /bigobj
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/include/phoenix/partition/ported/production_probe_compat.hpp")
else()
    target_compile_options(phoenix_inset_production_adapter_tests PRIVATE
        -Wall -Wextra -Wpedantic)
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

add_executable(phoenix_partition_working_face_tests
    src/geometry.cpp
    src/labels.cpp
    src/randomness.cpp
    src/partition/working_face.cpp
    src/partition/working_arrangement.cpp
    src/partition/arrangement_segment_repository.cpp
    src/partition/sampling.cpp
    src/partition/trusted_solver_foundation.cpp
    src/partition/ported_partition_solver_foundation.cpp
    src/partition/trusted_branching.cpp
    src/partition/adapted_constraints.cpp
    src/partition/plan_executor.cpp
    src/partition/straight_cut_tessellator.cpp
    src/partition/repeat_distribution.cpp
    tests/partition_working_face_tests.cpp
)
target_include_directories(phoenix_partition_working_face_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(phoenix_partition_working_face_tests PRIVATE CGAL::CGAL)
if(MSVC)
    target_compile_options(phoenix_partition_working_face_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(phoenix_partition_working_face_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(phoenix_partition_instruction_tests
    src/cache.cpp
    src/execution.cpp
    src/geometry.cpp
    src/graph.cpp
    src/labels.cpp
    src/publication.cpp
    src/randomness.cpp
    src/values.cpp
    src/working_geometry.cpp
    src/working_geometry_builder.cpp
    src/partition/instruction.cpp
    src/partition/working_face.cpp
    src/partition/production_adapter.cpp
    src/partition/ported/compatibility_runtime.cpp
    tests/partition_instruction_tests.cpp
    $<TARGET_OBJECTS:phoenix_partition_production_probe>
)
target_include_directories(phoenix_partition_instruction_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/phoenix/partition/ported/production"
)
target_link_libraries(phoenix_partition_instruction_tests PRIVATE CGAL::CGAL)
if(MSVC)
    target_compile_options(phoenix_partition_instruction_tests PRIVATE
        /W4 /permissive /Zc:strictStrings- /Zc:twoPhase- /bigobj
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/include/phoenix/partition/ported/production_probe_compat.hpp")
else()
    target_compile_options(phoenix_partition_instruction_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(phoenix_partition_plan_tests
    src/partition/repeat_distribution.cpp
    tests/partition_plan_tests.cpp
)
target_include_directories(phoenix_partition_plan_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(MSVC)
    target_compile_options(phoenix_partition_plan_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(phoenix_partition_plan_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(phoenix_partition_solver_view_tests
    src/geometry.cpp
    src/labels.cpp
    src/randomness.cpp
    src/partition/working_face.cpp
    src/partition/solver_view.cpp
    src/partition/angle_ranges.cpp
    src/partition/constraints.cpp
    src/partition/unconstrained_solver.cpp
    src/partition/sampling.cpp
    tests/partition_solver_view_tests.cpp
)
target_include_directories(phoenix_partition_solver_view_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(phoenix_partition_solver_view_tests PRIVATE CGAL::CGAL)
if(MSVC)
    target_compile_options(phoenix_partition_solver_view_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(phoenix_partition_solver_view_tests PRIVATE -Wall -Wextra -Wpedantic)
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
