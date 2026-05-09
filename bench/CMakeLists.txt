set(VR_BENCH_TARGET vr_bench_runner)

add_library(vr_bench_support STATIC
    support/bench_framework.cpp
    support/bench_crash_tracer.cpp
)

target_include_directories(vr_bench_support
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(vr_bench_support
    PUBLIC
        vulkan_init
)

if(VR_BENCH_CRASH_TRACER_ENABLED)
    target_link_libraries(vr_bench_support
        PRIVATE
            Center.Debug.CrashTracer
    )
    target_compile_definitions(vr_bench_support
        PRIVATE
            VR_BENCH_HAS_CRASH_TRACER=1
    )
endif()

add_executable(${VR_BENCH_TARGET}
    bench_main.cpp
    cases/vulkan_types_bench.cpp
    cases/ecs_text_component_bench.cpp
    cases/ecs_geometry_runtime_system_bench.cpp
    cases/ecs_bounds_system_bench.cpp
    cases/ecs_culling_system_bench.cpp
    cases/ecs_surface_runtime_system_bench.cpp
    cases/ecs_appearance_runtime_system_bench.cpp
    cases/ecs_surface_upload_plan_system_bench.cpp
    cases/ecs_text_runtime_system_bench.cpp
    cases/ecs_particle_runtime_system_bench.cpp
    cases/freetype_host_bench.cpp
    cases/glyph_atlas_host_bench.cpp
    cases/render_light_shadow_link_coordinator_bench.cpp
    cases/render_shadow_atlas_binding_coordinator_bench.cpp
    cases/frame_retire_host_bench.cpp
    cases/frame_sync_bench.cpp
    cases/runtime_diagnostics_bench.cpp
    cases/runtime_steady_state_allocation_bench.cpp
    cases/runtime_text_renderer_bench.cpp
    cases/scene_background_runtime_bench.cpp
)

target_link_libraries(${VR_BENCH_TARGET}
    PRIVATE
        vr_bench_support
        vulkan_platform_sdl
)

add_test(
    NAME vr_bench.smoke
    COMMAND ${VR_BENCH_TARGET}
            --filter FrameSyncHost_advance_frame_ring
            --warmup 1
            --runs 3
            --min-duration-ms 5
)
set_tests_properties(vr_bench.smoke PROPERTIES
    LABELS "bench;performance;fast"
)

add_test(
    NAME vr_bench.scene_background_smoke
    COMMAND ${VR_BENCH_TARGET}
            --filter ScenePrepare_dim3_resolve_environment_steady_state
            --warmup 1
            --runs 3
            --min-duration-ms 5
)
set_tests_properties(vr_bench.scene_background_smoke PROPERTIES
    LABELS "bench;performance;scene;background;fast"
)

add_test(
    NAME vr_bench.sky_environment_gpu_cache_hit_smoke
    COMMAND ${VR_BENCH_TARGET}
            --filter SkyEnvironmentGpuHost_register_or_update_static_cache_hit
            --warmup 1
            --runs 3
            --min-duration-ms 5
)
set_tests_properties(vr_bench.sky_environment_gpu_cache_hit_smoke PROPERTIES
    LABELS "bench;performance;scene;background;gpu_host;fast"
)

add_test(
    NAME vr_bench.scene_recorder_static_environment_smoke
    COMMAND ${VR_BENCH_TARGET}
            --filter SceneRecorder3D_prepare_static_environment_cpu
            --warmup 1
            --runs 3
            --min-duration-ms 5
)
set_tests_properties(vr_bench.scene_recorder_static_environment_smoke PROPERTIES
    LABELS "bench;performance;scene;background;recorder;fast"
)
