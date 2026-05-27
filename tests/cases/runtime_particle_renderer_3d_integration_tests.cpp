#include "support/test_framework.hpp"
#include "support/render_graph_test_utils.hpp"
#include "vr/ecs/system/bounds_system.hpp"
#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/particle_emitter_system.hpp"
#include "vr/ecs/system/particle_system.hpp"
#include "vr/ecs/system/transform_system.hpp"
#include "vr/particle/particle_renderer_3d.hpp"
#include "vr/particle/particle_simulation_host.hpp"
#include "vr/particle/particle_upload_host.hpp"
#include "vr/runtime/runtime.hpp"
#include "vr/render/render_view_submission_utils.hpp"
#include "vr/render/scene_recorder_3d.hpp"
#include "vr/render/scene_render_stage.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace {

using Runtime = vr::runtime::Runtime<vr::platform::ActiveBackendTag, 2U>;
using Particle3D = vr::ecs::Particle<vr::ecs::Dim3>;
using ParticleEmitter3D = vr::ecs::ParticleEmitter<vr::ecs::Dim3>;
using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
using Bounds3D = vr::ecs::Bounds<vr::ecs::Dim3>;
using Camera3D = vr::ecs::Camera<vr::ecs::Dim3>;

using ParticleSystem3D = vr::ecs::ParticleSystem<vr::ecs::Dim3>;
using ParticleEmitterSystem3D = vr::ecs::ParticleEmitterSystem<vr::ecs::Dim3>;
using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
using BoundsSystem3D = vr::ecs::BoundsSystem<vr::ecs::Dim3>;
using CameraSystem3D = vr::ecs::CameraSystem<vr::ecs::Dim3>;

[[nodiscard]] std::string ToLower(std::string_view value_) {
    std::string lowered{};
    lowered.reserve(value_.size());
    for (const unsigned char ch : value_) {
        lowered.push_back(static_cast<char>(std::tolower(ch)));
    }
    return lowered;
}

[[nodiscard]] bool ContainsCaseInsensitive(std::string_view text_,
                                           std::string_view needle_) {
    const std::string lowered_text = ToLower(text_);
    const std::string lowered_needle = ToLower(needle_);
    return lowered_text.find(lowered_needle) != std::string::npos;
}

[[nodiscard]] bool IsEnvironmentSkipError(std::string_view message_) {
    constexpr std::array<std::string_view, 18U> patterns{
        "sdl_initsubsystem",
        "sdl_createwindow",
        "sdl_vulkan_getinstanceextensions",
        "sdl_vulkan_createsurface",
        "vkcreateinstance",
        "vkenumeratephysicaldevices",
        "no vulkan physical devices found",
        "no suitable vulkan physical device found",
        "missing required instance extension",
        "vkcreatedevice",
        "vkgetphysicaldevicesurfacesupportkhr",
        "vkgetphysicaldevicesurfaceformatskhr",
        "vkgetphysicaldevicesurfacepresentmodeskhr",
        "bindlessresourcesystem",
        "descriptor indexing",
        "runtime descriptor array",
        "dynamicrendering",
        "synchronization2"
    };

    for (const auto pattern : patterns) {
        if (ContainsCaseInsensitive(message_, pattern)) {
            return true;
        }
    }
    return false;
}

void ConfigureParticle3DRuntimeCreateInfo(Runtime::CreateInfo& create_info_,
                                          const char* window_title_) {
    create_info_.platform.window.title = window_title_;
    create_info_.platform.window.width = 640;
    create_info_.platform.window.height = 360;
    create_info_.platform.window.resizable = true;
    create_info_.platform.window.high_pixel_density = true;
    create_info_.platform.instance.enable_validation = false;
    create_info_.platform.device.required_vulkan12_features.runtimeDescriptorArray = VK_TRUE;
    create_info_.platform.device.required_vulkan12_features.descriptorBindingPartiallyBound = VK_TRUE;
    create_info_.platform.device.required_vulkan12_features.descriptorBindingVariableDescriptorCount = VK_TRUE;
    create_info_.platform.device.required_vulkan12_features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    create_info_.platform.device.required_vulkan13_features.dynamicRendering = VK_TRUE;
    create_info_.platform.device.required_vulkan13_features.synchronization2 = VK_TRUE;
    create_info_.render_loop.swapchain.enable_vsync = false;
    create_info_.render_loop.swapchain.preferred_image_count = 2U;
    create_info_.render_loop.commands.initial_primary_per_frame = 2U;
    create_info_.render_loop.commands.primary_growth_chunk = 2U;
    create_info_.render_loop.submit_wait_stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    create_info_.diagnostics.level = vr::runtime::DiagnosticsLevel::Detailed;
    create_info_.poll_events_each_tick = true;
}

[[nodiscard]] bool HasEffectiveQueueBatchWithPass(
    const vr::runtime::RenderGraphRuntimeDiagnostics& diagnostics_,
    const vr::render_graph::QueueClass queue_,
    std::string_view pass_name_) {
    return std::any_of(
        diagnostics_.effective_queue_batches.begin(),
        diagnostics_.effective_queue_batches.end(),
        [&](const vr::runtime::RenderGraphQueueBatchDiagnostics& batch_) {
            if (batch_.queue != queue_) {
                return false;
            }
            return std::any_of(
                batch_.pass_debug_names.begin(),
                batch_.pass_debug_names.end(),
                [&](const std::string& debug_name_) {
                    return debug_name_ == pass_name_;
                });
        });
}

[[nodiscard]] bool HasEffectiveQueueBatchOnQueue(
    const vr::runtime::RenderGraphRuntimeDiagnostics& diagnostics_,
    const vr::render_graph::QueueClass queue_) {
    return std::any_of(
        diagnostics_.effective_queue_batches.begin(),
        diagnostics_.effective_queue_batches.end(),
        [&](const vr::runtime::RenderGraphQueueBatchDiagnostics& batch_) {
            return batch_.queue == queue_;
        });
}

[[nodiscard]] bool AllEffectiveQueueBatchesUseQueue(
    const vr::runtime::RenderGraphRuntimeDiagnostics& diagnostics_,
    const vr::render_graph::QueueClass queue_) {
    return !diagnostics_.effective_queue_batches.empty() &&
           std::all_of(
               diagnostics_.effective_queue_batches.begin(),
               diagnostics_.effective_queue_batches.end(),
               [&](const vr::runtime::RenderGraphQueueBatchDiagnostics& batch_) {
                   return batch_.queue == queue_;
               });
}

VR_TEST_CASE(RuntimeIntegration_particle_renderer_3d_transparent_stage_smoke,
             "integration;gpu;sdl;runtime;particle;render3d") {
    Runtime runtime{};
    vr::particle::ParticleRenderer3D particle_renderer{};

    bool runtime_initialized = false;
    bool renderer_initialized = false;

    try {
        Runtime::CreateInfo create_info{};
        ConfigureParticle3DRuntimeCreateInfo(create_info, "vr_tests_runtime_particle_3d");
        runtime.Initialize(create_info);
        runtime_initialized = true;

        Particle3D particle{};
        ParticleEmitter3D emitter{};
        ParticleSystem3D::Initialize(particle);
        ParticleEmitterSystem3D::Initialize(particle, emitter);
        ParticleSystem3D::SetRenderPassHint(particle, vr::ecs::ParticleRenderPassHint::transparent);
        ParticleSystem3D::SetSimulationMode(particle, vr::ecs::ParticleSimulationMode::hybrid_gpu);
        ParticleSystem3D::SetRenderMode(particle, vr::ecs::ParticleRenderMode::billboard);
        ParticleSystem3D::SetFacingMode(particle, vr::ecs::ParticleFacingMode::screen);
        ParticleSystem3D::SetStartEndColor(particle,
                                           vr::ecs::Rgba8{255U, 232U, 180U, 255U},
                                           vr::ecs::Rgba8{255U, 96U, 32U, 0U});
        ParticleSystem3D::SetScalarStyle(particle, 0.12F, 0.0F, 1.0F, 0.0F, 0.0F);
        ParticleSystem3D::SetDepthState(particle, true, false);

        ParticleEmitterSystem3D::SetBurst(particle, emitter, 10U, 0.0F);
        ParticleEmitterSystem3D::SetSpawnRate(particle, emitter, 60.0F);
        ParticleEmitterSystem3D::SetLifetimeRange(particle, emitter, 0.75F, 1.20F);
        ParticleEmitterSystem3D::SetSpeedRange(particle, emitter, 0.1F, 0.8F);
        ParticleEmitterSystem3D::SetSizeRange(particle, emitter, 0.20F, 0.32F, 0.04F, 0.12F);
        ParticleEmitterSystem3D::SetEmissionShape(particle,
                                                  emitter,
                                                  vr::ecs::ParticleEmitterShape::sphere,
                                                  vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                                  0.18F,
                                                  vr::ecs::Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F},
                                                  0.45F,
                                                  0.20F);
        ParticleEmitterSystem3D::SetPlayback(particle, emitter, true, true, true);

        Transform3D particle_transform{};
        Bounds3D particle_bounds{};
        TransformSystem3D::Initialize(particle_transform);
        BoundsSystem3D::Initialize(particle_bounds);
        TransformSystem3D::SetLocalPosition(particle_transform,
                                            vr::ecs::Float3{.x = 0.0F, .y = -0.15F, .z = 0.0F});
        BoundsSystem3D::SetLocalCenterExtents(particle_bounds,
                                              vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                              vr::ecs::Float3{.x = 0.55F, .y = 0.55F, .z = 0.55F});
        TransformSystem3D::UpdateHierarchy(&particle_transform, 1U);
        (void)BoundsSystem3D::UpdateAligned(&particle_bounds, &particle_transform, 1U);

        Camera3D camera{};
        Transform3D camera_transform{};
        CameraSystem3D::Initialize(camera);
        TransformSystem3D::Initialize(camera_transform);
        CameraSystem3D::SetProjectionMode(camera, vr::ecs::CameraProjectionMode::perspective);
        CameraSystem3D::SetVerticalFovRadians(camera, 60.0F * 0.01745329251994329577F);
        CameraSystem3D::SetNearFar(camera, 0.05F, 64.0F);
        CameraSystem3D::SetAspectRatio(camera, 640.0F / 360.0F);
        TransformSystem3D::SetLocalPosition(camera_transform,
                                            vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 3.25F});
        TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
        CameraSystem3D::MarkViewDirty(camera);
        CameraSystem3D::Update(camera, camera_transform);

        vr::particle::ParticleRenderer3DCreateInfo renderer_create_info{};
        renderer_create_info.reserve_component_count = 1U;
        renderer_create_info.reserve_particle_count = 256U;
        renderer_create_info.enable_depth = true;
        renderer_create_info.clear_depth = true;
        renderer_create_info.clear_depth_value = 0.46F;
        renderer_create_info.clear_swapchain = true;
        renderer_create_info.clear_color = VkClearColorValue{{0.14F, 0.25F, 0.39F, 1.0F}};
        particle_renderer.Initialize(renderer_create_info);
        renderer_initialized = true;
        runtime.Services().Get<vr::runtime::services::ParticleRenderService>().ConfigureRenderer(
            particle_renderer);
        particle_renderer.SetSceneData(&particle,
                                       &emitter,
                                       &particle_transform,
                                       1U,
                                       &camera,
                                       &camera_transform,
                                       &particle_bounds);

        std::uint32_t submitted_frames = 0U;
        std::uint32_t max_draw_calls = 0U;
        std::uint32_t max_transparent_draw_calls = 0U;
        std::uint32_t max_uploaded_instances = 0U;
        std::uint32_t max_indirect_draw_calls = 0U;
        std::uint32_t max_descriptor_binds = 0U;
        std::uint32_t max_descriptor_updates = 0U;
        bool observed_depth_interaction = false;
        bool observed_bounds_culling = false;
        bool opaque_pass_seen = false;
        bool transparent_pass_seen = false;
        bool opaque_pass_policy_seen = false;
        bool transparent_pass_policy_seen = false;
        bool opaque_descriptor_bindings_seen = false;
        bool transparent_descriptor_bindings_seen = false;

        constexpr std::uint32_t max_ticks = 12U;
        for (std::uint32_t tick_index = 0U;
             tick_index < max_ticks && runtime.IsRunning();
             ++tick_index) {
            const auto tick_result = runtime.Tick(particle_renderer);
            if (tick_result.render.code != vr::render::TickCode::Submitted) {
                continue;
            }
            ++submitted_frames;

            const auto& renderer_stats = particle_renderer.Stats();
            max_draw_calls = std::max(max_draw_calls, renderer_stats.draw_call_count);
            max_transparent_draw_calls = std::max(max_transparent_draw_calls,
                                                  renderer_stats.transparent_draw_call_count);
            max_uploaded_instances = std::max(max_uploaded_instances,
                                              renderer_stats.uploaded_instance_count);
            max_indirect_draw_calls = std::max(max_indirect_draw_calls,
                                               renderer_stats.indirect_draw_count);
            max_descriptor_binds = std::max(max_descriptor_binds,
                                            renderer_stats.descriptor_set_bind_count);
            max_descriptor_updates = std::max(max_descriptor_updates,
                                              renderer_stats.descriptor_set_update_count);
            observed_depth_interaction = observed_depth_interaction ||
                                         renderer_stats.depth_interaction_enabled;
            observed_bounds_culling = observed_bounds_culling ||
                                      renderer_stats.used_bounds_culling;
            const auto& graph_service =
                runtime.Services().Get<vr::runtime::services::RenderGraphRuntimeService>();
            if (const auto* compiled_graph = graph_service.TryGetCompiledGraph();
                compiled_graph != nullptr) {
                if (const auto* opaque_pass =
                        vr::test::FindCompiledPassByName(*compiled_graph,
                                                         "particle_renderer_3d_direct_opaque");
                    opaque_pass != nullptr &&
                    opaque_pass->executable &&
                    opaque_pass->raster_pass.has_value() &&
                    !opaque_pass->raster_pass->color_attachments.empty() &&
                    opaque_pass->raster_pass->has_depth_attachment) {
                    opaque_pass_seen = true;
                    const auto& color_attachment =
                        opaque_pass->raster_pass->color_attachments.front();
                    const auto& depth_attachment = opaque_pass->raster_pass->depth_attachment;
                    opaque_pass_policy_seen =
                        opaque_pass_policy_seen ||
                        (color_attachment.load_op ==
                             vr::render_graph::AttachmentLoadOp::clear &&
                         depth_attachment.load_op ==
                             vr::render_graph::AttachmentLoadOp::clear &&
                         depth_attachment.clear_value.depth ==
                             renderer_create_info.clear_depth_value &&
                         color_attachment.clear_value.red ==
                             renderer_create_info.clear_color.float32[0] &&
                         color_attachment.clear_value.green ==
                             renderer_create_info.clear_color.float32[1] &&
                         color_attachment.clear_value.blue ==
                             renderer_create_info.clear_color.float32[2] &&
                         color_attachment.clear_value.alpha ==
                             renderer_create_info.clear_color.float32[3]);
                    opaque_descriptor_bindings_seen =
                        opaque_descriptor_bindings_seen ||
                        !opaque_pass->descriptor_bindings.empty();
                }
                if (const auto* transparent_pass =
                        vr::test::FindCompiledPassByName(*compiled_graph,
                                                         "particle_renderer_3d_direct_transparent");
                    transparent_pass != nullptr &&
                    transparent_pass->executable &&
                    transparent_pass->raster_pass.has_value() &&
                    !transparent_pass->raster_pass->color_attachments.empty() &&
                    transparent_pass->raster_pass->has_depth_attachment) {
                    transparent_pass_seen = true;
                    const auto& color_attachment =
                        transparent_pass->raster_pass->color_attachments.front();
                    const auto& depth_attachment =
                        transparent_pass->raster_pass->depth_attachment;
                    transparent_pass_policy_seen =
                        transparent_pass_policy_seen ||
                        (color_attachment.load_op ==
                             vr::render_graph::AttachmentLoadOp::load &&
                         depth_attachment.load_op ==
                             vr::render_graph::AttachmentLoadOp::load);
                    transparent_descriptor_bindings_seen =
                        transparent_descriptor_bindings_seen ||
                        !transparent_pass->descriptor_bindings.empty();
                }
            }
        }

        VR_REQUIRE(submitted_frames > 0U);
        VR_REQUIRE(max_uploaded_instances > 0U);
        VR_REQUIRE(max_draw_calls > 0U);
        VR_REQUIRE(max_transparent_draw_calls > 0U);
        VR_REQUIRE(max_descriptor_binds > 0U);
        VR_CHECK(max_descriptor_binds <= 4U);
        VR_CHECK(max_descriptor_updates == 0U);
        VR_REQUIRE(observed_depth_interaction);
        VR_REQUIRE(observed_bounds_culling);
        VR_CHECK(opaque_pass_seen);
        VR_CHECK(transparent_pass_seen);
        VR_CHECK(opaque_pass_policy_seen);
        VR_CHECK(transparent_pass_policy_seen);
        VR_CHECK(opaque_descriptor_bindings_seen);
        VR_CHECK(transparent_descriptor_bindings_seen);
        const auto& particle_simulation_service =
            runtime.Services().Get<vr::runtime::services::ParticleSimulationService>();
        VR_REQUIRE(particle_simulation_service.Stats().prepared_frame_count > 0U);
        if (particle_simulation_service.Capabilities().SupportsHybridSimulation()) {
            VR_REQUIRE(particle_simulation_service.Stats().gpu_build_prepare_count > 0U);
            VR_REQUIRE(particle_simulation_service.Stats().gpu_build_dispatch_count > 0U);
            VR_REQUIRE(particle_simulation_service.Stats().update_dispatch_count > 0U);
            VR_REQUIRE(max_indirect_draw_calls > 0U);
        }

        particle_renderer.Shutdown(runtime.Context());
        renderer_initialized = false;
        runtime.Shutdown();
        runtime_initialized = false;
    } catch (const std::exception& e) {
        if (IsEnvironmentSkipError(e.what())) {
            if (renderer_initialized) {
                particle_renderer.Shutdown(runtime.Context());
                renderer_initialized = false;
            }
            if (runtime_initialized) {
                runtime.Shutdown();
                runtime_initialized = false;
            }
            VR_SKIP(e.what());
        }
        if (renderer_initialized) {
            particle_renderer.Shutdown(runtime.Context());
            renderer_initialized = false;
        }
        if (runtime_initialized) {
            runtime.Shutdown();
            runtime_initialized = false;
        }
        throw;
    }
}

VR_TEST_CASE(RuntimeIntegration_particle_renderer_3d_gpu_persistent_seed_once,
             "integration;gpu;sdl;runtime;particle;render3d") {
    Runtime runtime{};
    vr::particle::ParticleRenderer3D particle_renderer{};

    bool runtime_initialized = false;
    bool renderer_initialized = false;

    try {
        Runtime::CreateInfo create_info{};
        ConfigureParticle3DRuntimeCreateInfo(create_info, "vr_tests_runtime_particle_3d_gpu");
        runtime.Initialize(create_info);
        runtime_initialized = true;

        Particle3D particle{};
        ParticleEmitter3D emitter{};
        ParticleSystem3D::Initialize(particle);
        ParticleEmitterSystem3D::Initialize(particle, emitter);
        ParticleSystem3D::SetRenderPassHint(particle, vr::ecs::ParticleRenderPassHint::transparent);
        ParticleSystem3D::SetSimulationMode(particle, vr::ecs::ParticleSimulationMode::gpu);
        ParticleSystem3D::SetSortMode(particle, vr::ecs::ParticleSortMode::gpu_radix);
        ParticleSystem3D::SetRenderMode(particle, vr::ecs::ParticleRenderMode::billboard);
        ParticleSystem3D::SetFacingMode(particle, vr::ecs::ParticleFacingMode::screen);
        ParticleSystem3D::SetStartEndColor(particle,
                                           vr::ecs::Rgba8{255U, 232U, 180U, 255U},
                                           vr::ecs::Rgba8{255U, 96U, 32U, 0U});
        ParticleSystem3D::SetScalarStyle(particle, 0.12F, 0.0F, 1.0F, 0.0F, 0.0F);
        ParticleSystem3D::SetDepthState(particle, true, false);

        ParticleEmitterSystem3D::SetBurst(particle, emitter, 18U, 0.0F);
        ParticleEmitterSystem3D::SetSpawnRate(particle, emitter, 0.0F);
        ParticleEmitterSystem3D::SetLifetimeRange(particle, emitter, 1.20F, 1.60F);
        ParticleEmitterSystem3D::SetSpeedRange(particle, emitter, 0.15F, 0.70F);
        ParticleEmitterSystem3D::SetSizeRange(particle, emitter, 0.18F, 0.30F, 0.05F, 0.11F);
        ParticleEmitterSystem3D::SetSimulationSpace(particle,
                                                    emitter,
                                                    vr::ecs::ParticleSimulationSpace::world);
        ParticleEmitterSystem3D::SetEmissionShape(particle,
                                                  emitter,
                                                  vr::ecs::ParticleEmitterShape::sphere,
                                                  vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                                  0.16F,
                                                  vr::ecs::Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F},
                                                  0.35F,
                                                  0.18F);
        ParticleEmitterSystem3D::SetPlayback(particle, emitter, true, false, true);

        Transform3D particle_transform{};
        Bounds3D particle_bounds{};
        TransformSystem3D::Initialize(particle_transform);
        BoundsSystem3D::Initialize(particle_bounds);
        TransformSystem3D::SetLocalPosition(particle_transform,
                                            vr::ecs::Float3{.x = 0.0F, .y = -0.10F, .z = 0.0F});
        BoundsSystem3D::SetLocalCenterExtents(particle_bounds,
                                              vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                              vr::ecs::Float3{.x = 0.60F, .y = 0.60F, .z = 0.60F});
        TransformSystem3D::UpdateHierarchy(&particle_transform, 1U);
        (void)BoundsSystem3D::UpdateAligned(&particle_bounds, &particle_transform, 1U);

        Camera3D camera{};
        Transform3D camera_transform{};
        CameraSystem3D::Initialize(camera);
        TransformSystem3D::Initialize(camera_transform);
        CameraSystem3D::SetProjectionMode(camera, vr::ecs::CameraProjectionMode::perspective);
        CameraSystem3D::SetVerticalFovRadians(camera, 60.0F * 0.01745329251994329577F);
        CameraSystem3D::SetNearFar(camera, 0.05F, 64.0F);
        CameraSystem3D::SetAspectRatio(camera, 640.0F / 360.0F);
        TransformSystem3D::SetLocalPosition(camera_transform,
                                            vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 3.0F});
        TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
        CameraSystem3D::MarkViewDirty(camera);
        CameraSystem3D::Update(camera, camera_transform);

        vr::particle::ParticleRenderer3DCreateInfo renderer_create_info{};
        renderer_create_info.reserve_component_count = 1U;
        renderer_create_info.reserve_particle_count = 256U;
        renderer_create_info.enable_depth = true;
        renderer_create_info.clear_depth = true;
        renderer_create_info.clear_swapchain = false;
        particle_renderer.Initialize(renderer_create_info);
        renderer_initialized = true;
        runtime.Services().Get<vr::runtime::services::ParticleRenderService>().ConfigureRenderer(
            particle_renderer);
        particle_renderer.SetSceneData(&particle,
                                       &emitter,
                                       &particle_transform,
                                       1U,
                                       &camera,
                                       &camera_transform,
                                       &particle_bounds);

        std::uint32_t submitted_frames = 0U;
        std::uint32_t max_indirect_draw_calls = 0U;
        std::uint32_t max_uploaded_instances = 0U;
        std::uint32_t max_draw_calls = 0U;
        std::uint32_t max_descriptor_binds = 0U;
        std::uint32_t max_descriptor_updates = 0U;

        constexpr std::uint32_t max_ticks = 10U;
        for (std::uint32_t tick_index = 0U;
             tick_index < max_ticks && runtime.IsRunning();
             ++tick_index) {
            const auto tick_result = runtime.Tick(particle_renderer);
            if (tick_result.render.code != vr::render::TickCode::Submitted) {
                continue;
            }
            ++submitted_frames;
            const auto& renderer_stats = particle_renderer.Stats();
            max_indirect_draw_calls = std::max(max_indirect_draw_calls,
                                               renderer_stats.indirect_draw_count);
            max_uploaded_instances = std::max(max_uploaded_instances,
                                              renderer_stats.uploaded_instance_count);
            max_draw_calls = std::max(max_draw_calls,
                                      renderer_stats.draw_call_count);
            max_descriptor_binds = std::max(max_descriptor_binds,
                                            renderer_stats.descriptor_set_bind_count);
            max_descriptor_updates = std::max(max_descriptor_updates,
                                              renderer_stats.descriptor_set_update_count);
        }

        VR_REQUIRE(submitted_frames > 0U);
        VR_REQUIRE(max_indirect_draw_calls > 0U);
        VR_REQUIRE(max_uploaded_instances > 0U);
        VR_REQUIRE(max_draw_calls > 0U);
        VR_REQUIRE(max_descriptor_binds > 0U);
        VR_CHECK(max_descriptor_binds <= 4U);
        VR_CHECK(max_descriptor_updates == 0U);
        const auto& particle_simulation_service_gpu =
            runtime.Services().Get<vr::runtime::services::ParticleSimulationService>();
        VR_REQUIRE(particle_simulation_service_gpu.Stats().prepared_frame_count > 0U);
        if (particle_simulation_service_gpu.Capabilities().SupportsGpuSimulation()) {
            VR_REQUIRE(particle_simulation_service_gpu.Stats().gpu_build_prepare_count > 0U);
            VR_REQUIRE(particle_simulation_service_gpu.Stats().gpu_build_dispatch_count > 0U);
            VR_REQUIRE(particle_simulation_service_gpu.Stats().update_dispatch_count > 0U);
            VR_REQUIRE(particle_simulation_service_gpu.Stats().state_upload_count > 0U);
            VR_REQUIRE(particle_simulation_service_gpu.Stats().state_upload_count <=
                       particle_simulation_service_gpu.Host().FramesInFlight());
            VR_REQUIRE(particle_simulation_service_gpu.Stats().spawn_packet_upload_count <=
                       particle_simulation_service_gpu.Host().FramesInFlight());
            VR_REQUIRE(particle_renderer.Stats().cache_reused);
        }

        particle_renderer.Shutdown(runtime.Context());
        renderer_initialized = false;
        runtime.Shutdown();
        runtime_initialized = false;
    } catch (const std::exception& e) {
        if (IsEnvironmentSkipError(e.what())) {
        if (renderer_initialized) {
            particle_renderer.Shutdown(runtime.Context());
            renderer_initialized = false;
        }
        if (runtime_initialized) {
            runtime.Shutdown();
            runtime_initialized = false;
            }
            VR_SKIP(e.what());
        }
        if (renderer_initialized) {
            particle_renderer.Shutdown(runtime.Context());
            renderer_initialized = false;
        }
        if (runtime_initialized) {
            runtime.Shutdown();
            runtime_initialized = false;
        }
        throw;
    }
}

VR_TEST_CASE(RuntimeIntegration_scene_recorder_3d_particle_graph_async_compute_uses_compute_or_graphics_fallback,
             "integration;gpu;sdl;runtime;particle;scene3d;render_graph;queue;compute") {
    Runtime runtime{};
    vr::render::SceneRecorder3D recorder{};
    vr::particle::ParticleRenderer3D particle_renderer{};

    bool runtime_initialized = false;
    bool recorder_initialized = false;
    bool renderer_initialized = false;

    try {
        Runtime::CreateInfo create_info{};
        ConfigureParticle3DRuntimeCreateInfo(create_info,
                                             "vr_tests_scene_recorder_3d_particle_graph_compute");
        runtime.Initialize(create_info);
        runtime_initialized = true;

        const auto& particle_simulation_service =
            runtime.Services().Get<vr::runtime::services::ParticleSimulationService>();
        if (!particle_simulation_service.Capabilities().SupportsHybridSimulation()) {
            runtime.Shutdown();
            runtime_initialized = false;
            VR_SKIP("ParticleSimulationService hybrid GPU simulation is unavailable in this environment.");
        }

        Particle3D particle{};
        ParticleEmitter3D emitter{};
        ParticleSystem3D::Initialize(particle);
        ParticleEmitterSystem3D::Initialize(particle, emitter);
        ParticleSystem3D::SetRenderPassHint(particle, vr::ecs::ParticleRenderPassHint::transparent);
        ParticleSystem3D::SetSimulationMode(particle, vr::ecs::ParticleSimulationMode::hybrid_gpu);
        ParticleSystem3D::SetRenderMode(particle, vr::ecs::ParticleRenderMode::billboard);
        ParticleSystem3D::SetFacingMode(particle, vr::ecs::ParticleFacingMode::screen);
        ParticleSystem3D::SetDepthState(particle, true, false);
        ParticleSystem3D::SetStartEndColor(particle,
                                           vr::ecs::Rgba8{255U, 232U, 180U, 255U},
                                           vr::ecs::Rgba8{255U, 96U, 32U, 0U});
        ParticleSystem3D::SetScalarStyle(particle, 0.12F, 0.0F, 1.0F, 0.0F, 0.0F);

        ParticleEmitterSystem3D::SetBurst(particle, emitter, 16U, 0.0F);
        ParticleEmitterSystem3D::SetSpawnRate(particle, emitter, 42.0F);
        ParticleEmitterSystem3D::SetLifetimeRange(particle, emitter, 0.75F, 1.20F);
        ParticleEmitterSystem3D::SetSpeedRange(particle, emitter, 0.1F, 0.8F);
        ParticleEmitterSystem3D::SetSizeRange(particle, emitter, 0.20F, 0.32F, 0.04F, 0.12F);
        ParticleEmitterSystem3D::SetEmissionShape(particle,
                                                  emitter,
                                                  vr::ecs::ParticleEmitterShape::sphere,
                                                  vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                                  0.18F,
                                                  vr::ecs::Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F},
                                                  0.45F,
                                                  0.20F);
        ParticleEmitterSystem3D::SetPlayback(particle, emitter, true, true, true);

        Transform3D particle_transform{};
        Bounds3D particle_bounds{};
        TransformSystem3D::Initialize(particle_transform);
        BoundsSystem3D::Initialize(particle_bounds);
        TransformSystem3D::SetLocalPosition(particle_transform,
                                            vr::ecs::Float3{.x = 0.0F, .y = -0.15F, .z = 0.0F});
        BoundsSystem3D::SetLocalCenterExtents(particle_bounds,
                                              vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                              vr::ecs::Float3{.x = 0.55F, .y = 0.55F, .z = 0.55F});
        TransformSystem3D::UpdateHierarchy(&particle_transform, 1U);
        (void)BoundsSystem3D::UpdateAligned(&particle_bounds, &particle_transform, 1U);

        Camera3D camera{};
        Transform3D camera_transform{};
        CameraSystem3D::Initialize(camera);
        TransformSystem3D::Initialize(camera_transform);
        CameraSystem3D::SetProjectionMode(camera, vr::ecs::CameraProjectionMode::perspective);
        CameraSystem3D::SetVerticalFovRadians(camera, 60.0F * 0.01745329251994329577F);
        CameraSystem3D::SetNearFar(camera, 0.05F, 64.0F);
        CameraSystem3D::SetAspectRatio(camera, 640.0F / 360.0F);
        TransformSystem3D::SetLocalPosition(camera_transform,
                                            vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 3.25F});
        TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
        CameraSystem3D::MarkViewDirty(camera);
        CameraSystem3D::Update(camera, camera_transform);

        vr::render::SceneRecorder3DCreateInfo recorder_create_info{};
        recorder_create_info.scene_target.color_debug_name = "RuntimeParticle3DGraphSceneColor";
        recorder_create_info.scene_target.depth_debug_name = "RuntimeParticle3DGraphSceneDepth";
        recorder_create_info.scene_target.enable_depth = true;
        recorder_create_info.scene_target.color_lifetime = vr::render::RenderTargetLifetime::transient;
        recorder_create_info.scene_target.depth_lifetime = vr::render::RenderTargetLifetime::transient;
        recorder_create_info.reserve_scene_renderer_count = 1U;
        recorder_create_info.reserve_overlay_renderer_count = 0U;
        recorder.Initialize(recorder_create_info);
        recorder_initialized = true;
        recorder.BindRuntime(runtime);

        vr::particle::ParticleRenderer3DCreateInfo renderer_create_info{};
        renderer_create_info.reserve_component_count = 1U;
        renderer_create_info.reserve_particle_count = 256U;
        renderer_create_info.enable_depth = true;
        renderer_create_info.clear_depth = true;
        renderer_create_info.clear_swapchain = false;
        particle_renderer.Initialize(renderer_create_info);
        renderer_initialized = true;
        runtime.Services().Get<vr::runtime::services::ParticleRenderService>().ConfigureRenderer(
            particle_renderer);
        particle_renderer.SetSceneData(&particle,
                                       &emitter,
                                       &particle_transform,
                                       1U,
                                       &camera,
                                       &camera_transform,
                                       &particle_bounds);
        recorder.RegisterTransparentSceneRenderer(particle_renderer,
                                                  vr::render::SceneRenderPassRole::single);

        vr::render::RenderView3D main_view{};
        vr::render::RenderScenePacket3D main_scene_packet{};
        vr::render::RefreshExtentBoundWorldSceneSubmission(main_view,
                                                           main_scene_packet,
                                                           camera,
                                                           camera_transform,
                                                           runtime.Swapchain().Extent(),
                                                           9201U);
        recorder.SetFramePacket(&main_scene_packet);

        std::uint32_t submitted_frames = 0U;
        std::uint32_t max_indirect_draw_calls = 0U;
        bool graph_only_active = false;
        bool compute_requested = false;
        bool compute_enabled = false;
        bool graphics_fallback_active = false;
        bool graph_compute_pass_seen = false;
        bool queue_timeline_json_available = false;
        std::uint32_t max_effective_queue_batch_count = 0U;
        std::uint32_t max_effective_queue_dependency_count = 0U;
        bool compute_queue_batch_seen = false;
        bool graphics_queue_batch_seen = false;
        bool graphics_only_effective_batches_seen = false;
        std::uint64_t max_compute_submitted = 0U;
        std::uint64_t max_compute_completed = 0U;

        constexpr std::uint32_t max_ticks = 8U;
        for (std::uint32_t tick_index = 0U;
             tick_index < max_ticks && runtime.IsRunning();
             ++tick_index) {
            vr::render::RefreshExtentBoundWorldSceneSubmission(main_view,
                                                               main_scene_packet,
                                                               camera,
                                                               camera_transform,
                                                               runtime.Swapchain().Extent(),
                                                               9201U + tick_index);
            recorder.SetFramePacket(&main_scene_packet);

            const auto tick_result = runtime.Tick(recorder);
            if (tick_result.render.code == vr::render::TickCode::Submitted ||
                tick_result.render.code == vr::render::TickCode::RecreateRequested) {
                ++submitted_frames;
            }

            const auto& graph_diag = tick_result.diagnostics.render_graph;
            graph_only_active =
                graph_only_active || vr::test::IsGraphOnlyScene3DRecordActive(runtime);
            compute_requested = compute_requested || graph_diag.compute_queue_requested;
            compute_enabled = compute_enabled || graph_diag.compute_queue_enabled;
            graphics_fallback_active =
                graphics_fallback_active || graph_diag.graphics_fallback_active;
            queue_timeline_json_available =
                queue_timeline_json_available ||
                !vr::runtime::BuildRenderGraphQueueTimelineJson(graph_diag).empty();
            max_effective_queue_batch_count =
                std::max(max_effective_queue_batch_count,
                         graph_diag.effective_queue_batch_count);
            max_effective_queue_dependency_count =
                std::max(max_effective_queue_dependency_count,
                         graph_diag.effective_queue_dependency_count);
            compute_queue_batch_seen =
                compute_queue_batch_seen ||
                HasEffectiveQueueBatchOnQueue(
                    graph_diag,
                    vr::render_graph::QueueClass::compute);
            graphics_queue_batch_seen =
                graphics_queue_batch_seen ||
                HasEffectiveQueueBatchOnQueue(
                    graph_diag,
                    vr::render_graph::QueueClass::graphics);
            graphics_only_effective_batches_seen =
                graphics_only_effective_batches_seen ||
                AllEffectiveQueueBatchesUseQueue(
                    graph_diag,
                    vr::render_graph::QueueClass::graphics);
            max_indirect_draw_calls = std::max(max_indirect_draw_calls,
                                               particle_renderer.Stats().indirect_draw_count);
            max_compute_submitted = std::max(max_compute_submitted,
                                             tick_result.diagnostics.queues.compute_submitted);
            max_compute_completed = std::max(max_compute_completed,
                                             tick_result.diagnostics.queues.compute_completed);

            const auto& graph_service =
                runtime.Services().Get<vr::runtime::services::RenderGraphRuntimeService>();
            if (const auto* compiled_graph = graph_service.TryGetCompiledGraph();
                compiled_graph != nullptr) {
                graph_compute_pass_seen = graph_compute_pass_seen ||
                    std::any_of(compiled_graph->Passes().begin(),
                                compiled_graph->Passes().end(),
                                [](const auto& pass_) {
                                    return pass_.debug_name == "particle_3d_gpu_build" &&
                                           pass_.executable;
                                });
            }

            SDL_Delay(1U);
        }

        VR_REQUIRE(submitted_frames > 0U);
        VR_CHECK(graph_only_active);
        VR_CHECK(queue_timeline_json_available);
        VR_CHECK(max_effective_queue_batch_count > 0U);
        VR_REQUIRE(particle_simulation_service.Stats().gpu_build_prepare_count > 0U);
        VR_REQUIRE(particle_simulation_service.Stats().gpu_build_dispatch_count > 0U);
        VR_REQUIRE(particle_simulation_service.Stats().update_dispatch_count > 0U);
        VR_REQUIRE(max_indirect_draw_calls > 0U);
        VR_CHECK(recorder.Stats().frame_packet_record_count == 0U);

        if (compute_enabled) {
            VR_CHECK(graph_compute_pass_seen);
            VR_CHECK(compute_requested);
            VR_CHECK(!graphics_fallback_active);
            VR_CHECK(max_effective_queue_dependency_count > 0U);
            VR_CHECK(compute_queue_batch_seen);
            VR_CHECK(graphics_queue_batch_seen);
            VR_CHECK(max_compute_submitted > 0U);
            VR_CHECK(max_compute_completed <= max_compute_submitted);
        } else {
            VR_CHECK(graphics_fallback_active || !compute_requested);
            VR_CHECK(graphics_only_effective_batches_seen);
            VR_CHECK(max_compute_submitted == 0U);
        }

        recorder.Shutdown(runtime.Context());
        recorder_initialized = false;
        particle_renderer.Shutdown(runtime.Context());
        renderer_initialized = false;
        runtime.Shutdown();
        runtime_initialized = false;
    } catch (const std::exception& e) {
        if (recorder_initialized && runtime_initialized && runtime.IsInitialized()) {
            recorder.Shutdown(runtime.Context());
            recorder_initialized = false;
        }
        if (renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            particle_renderer.Shutdown(runtime.Context());
            renderer_initialized = false;
        }
        if (runtime_initialized && runtime.IsInitialized()) {
            runtime.Shutdown();
            runtime_initialized = false;
        }
        if (IsEnvironmentSkipError(e.what())) {
            VR_SKIP(e.what());
        }
        throw;
    }
}

} // namespace

