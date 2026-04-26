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
    cases/vulkan_types_tests.cpp
    cases/frame_retire_host_tests.cpp
    cases/ecs_geometry_component_tests.cpp
    cases/ecs_geometry_batch_system_tests.cpp
    cases/ecs_geometry_runtime_system_tests.cpp
    cases/ecs_text_component_tests.cpp
    cases/ecs_transform_camera_system_tests.cpp
    cases/ecs_text_render_3d_system_tests.cpp
    cases/ecs_text_runtime_system_tests.cpp
    cases/freetype_host_tests.cpp
    cases/glyph_atlas_host_tests.cpp
    cases/runtime_configuration_tests.cpp
    cases/runtime_integration_tests.cpp
    cases/runtime_text_renderer_integration_tests.cpp
    cases/runtime_text_renderer_3d_integration_tests.cpp
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
