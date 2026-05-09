set(VR_TEST_TARGET vr_tests)

add_library(vr_test_support STATIC
    support/test_framework.cpp
)

target_include_directories(vr_test_support
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(vr_test_support
    PUBLIC
        vulkan_init
)

add_executable(${VR_TEST_TARGET}
    test_main.cpp
    cases/animation_host_tests.cpp
    cases/animation_vertex_frame_host_tests.cpp
    cases/animation_skeletal_morph_host_tests.cpp
    cases/ecs_animation_evaluation_system_tests.cpp
    cases/ecs_animation_deformation_evaluation_system_tests.cpp
    cases/ecs_animation_vertex_frame_evaluation_system_tests.cpp
    cases/vulkan_types_tests.cpp
    cases/frame_retire_host_tests.cpp
    cases/particle_simulation_host_tests.cpp
    cases/ecs_geometry_component_tests.cpp
    cases/ecs_geometry_batch_system_tests.cpp
    cases/ecs_geometry_runtime_system_tests.cpp
    cases/ecs_text_component_tests.cpp
    cases/ecs_light_component_tests.cpp
    cases/ecs_animation_system_tests.cpp
    cases/ecs_shadow_component_tests.cpp
    cases/ecs_surface_component_tests.cpp
    cases/ecs_particle_component_tests.cpp
    cases/ecs_appearance_component_tests.cpp
    cases/ecs_appearance_system_tests.cpp
    cases/ecs_appearance_runtime_system_tests.cpp
    cases/ecs_appearance_link_system_tests.cpp
    cases/render_appearance_frame_coordinator_tests.cpp
    cases/scene_background_core_tests.cpp
    cases/render_light_frame_coordinator_tests.cpp
    cases/render_light_shadow_link_coordinator_tests.cpp
    cases/render_light_shadow_link_stage_tests.cpp
    cases/render_shadow_atlas_binding_coordinator_tests.cpp
    cases/render_shadow_frame_coordinator_tests.cpp
    cases/render_target_types_tests.cpp
    cases/ecs_bounds_system_tests.cpp
    cases/ecs_culling_system_tests.cpp
    cases/ecs_surface_batch_runtime_system_tests.cpp
    cases/ecs_surface_upload_plan_system_tests.cpp
    cases/ecs_transform_camera_system_tests.cpp
    cases/ecs_text_render_3d_system_tests.cpp
    cases/ecs_text_runtime_system_tests.cpp
    cases/ecs_particle_runtime_system_tests.cpp
    cases/ecs_light_runtime_system_tests.cpp
    cases/ecs_light_culling_system_tests.cpp
    cases/ecs_shadow_runtime_system_tests.cpp
    cases/ecs_shadow_caster_system_tests.cpp
    cases/shadow_renderer_3d_lifecycle_tests.cpp
    cases/freetype_host_tests.cpp
    cases/glyph_atlas_host_tests.cpp
    cases/runtime_configuration_tests.cpp
    cases/runtime_ibl_bake_host_integration_tests.cpp
    cases/runtime_ibl_host_integration_tests.cpp
    cases/runtime_integration_tests.cpp
    cases/runtime_text_renderer_integration_tests.cpp
    cases/runtime_text_renderer_3d_integration_tests.cpp
    cases/runtime_geometry_material_host_tests.cpp
    cases/runtime_texture_host_integration_tests.cpp
    cases/runtime_geometry_image_host_integration_tests.cpp
    cases/surface_upload_host_policy_tests.cpp
    cases/runtime_surface_upload_host_integration_tests.cpp
    cases/runtime_particle_renderer_2d_integration_tests.cpp
    cases/runtime_particle_renderer_3d_integration_tests.cpp
    cases/runtime_surface_renderer_3d_integration_tests.cpp
    cases/runtime_geometry_renderer_2d_integration_tests.cpp
    cases/runtime_geometry_renderer_3d_integration_tests.cpp
    cases/runtime_scene_3d_unified_integration_tests.cpp
)

target_link_libraries(${VR_TEST_TARGET}
    PRIVATE
        vr_test_support
        vulkan_platform_sdl
)

add_test(
    NAME vr_tests.unit
    COMMAND ${VR_TEST_TARGET} --exclude-tag integration
)
set_tests_properties(vr_tests.unit PROPERTIES LABELS "unit;fast")

add_test(
    NAME vr_tests.integration
    COMMAND ${VR_TEST_TARGET} --include-tag integration --return-on-all-skipped 125
)
set_tests_properties(vr_tests.integration PROPERTIES
    SKIP_RETURN_CODE 125
    LABELS "integration;gpu"
)
