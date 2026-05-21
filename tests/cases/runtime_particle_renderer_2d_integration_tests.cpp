#include "support/render_graph_test_utils.hpp"
#include "support/test_framework.hpp"
#include "vr/ecs/system/particle_emitter_system.hpp"
#include "vr/ecs/system/particle_system.hpp"
#include "vr/ecs/system/transform_system.hpp"
#include "vr/particle/particle_renderer_2d.hpp"
#include "vr/particle/particle_simulation_host.hpp"
#include "vr/particle/particle_upload_host.hpp"
#include "vr/runtime/runtime.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace {

using Runtime = vr::runtime::Runtime<vr::platform::ActiveBackendTag, 2U>;
using Particle2D = vr::ecs::Particle<vr::ecs::Dim2>;
using ParticleEmitter2D = vr::ecs::ParticleEmitter<vr::ecs::Dim2>;
using Transform2D = vr::ecs::Transform<vr::ecs::Dim2>;

using ParticleSystem2D = vr::ecs::ParticleSystem<vr::ecs::Dim2>;
using ParticleEmitterSystem2D = vr::ecs::ParticleEmitterSystem<vr::ecs::Dim2>;
using TransformSystem2D = vr::ecs::TransformSystem<vr::ecs::Dim2>;

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

void ConfigureParticle2DRuntimeCreateInfo(Runtime::CreateInfo& create_info_,
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
    create_info_.poll_events_each_tick = true;
}

VR_TEST_CASE(RuntimeIntegration_particle_renderer_2d_hybrid_smoke,
             "integration;gpu;sdl;runtime;particle;render2d") {
    Runtime runtime{};
    vr::particle::ParticleRenderer2D particle_renderer{};

    bool runtime_initialized = false;
    bool renderer_initialized = false;

    try {
        Runtime::CreateInfo create_info{};
        ConfigureParticle2DRuntimeCreateInfo(create_info, "vr_tests_runtime_particle_2d");
        runtime.Initialize(create_info);
        runtime_initialized = true;

        Particle2D particle{};
        ParticleEmitter2D emitter{};
        ParticleSystem2D::Initialize(particle);
        ParticleEmitterSystem2D::Initialize(particle, emitter);
        ParticleSystem2D::SetRenderPassHint(particle, vr::ecs::ParticleRenderPassHint::transparent);
        ParticleSystem2D::SetSimulationMode(particle, vr::ecs::ParticleSimulationMode::hybrid_gpu);
        ParticleSystem2D::SetRenderMode(particle, vr::ecs::ParticleRenderMode::axis_aligned);
        ParticleSystem2D::SetBlendMode(particle, vr::ecs::ParticleBlendMode::premultiplied_alpha);
        ParticleSystem2D::SetPremultipliedAlpha(particle, true);
        ParticleSystem2D::SetStartEndColor(particle,
                                           vr::ecs::Rgba8{255U, 220U, 120U, 255U},
                                           vr::ecs::Rgba8{255U, 80U, 16U, 0U});
        ParticleSystem2D::SetScalarStyle(particle, 24.0F, 0.0F, 1.0F, 0.0F, 0.0F);

        ParticleEmitterSystem2D::SetBurst(particle, emitter, 12U, 0.0F);
        ParticleEmitterSystem2D::SetSpawnRate(particle, emitter, 48.0F);
        ParticleEmitterSystem2D::SetLifetimeRange(particle, emitter, 0.60F, 1.10F);
        ParticleEmitterSystem2D::SetSpeedRange(particle, emitter, 12.0F, 40.0F);
        ParticleEmitterSystem2D::SetSizeRange(particle, emitter, 16.0F, 28.0F, 4.0F, 10.0F);
        ParticleEmitterSystem2D::SetEmissionShape(particle,
                                                  emitter,
                                                  vr::ecs::ParticleEmitterShape::circle,
                                                  vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                                  8.0F,
                                                  vr::ecs::Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F},
                                                  0.0F,
                                                  0.0F);
        ParticleEmitterSystem2D::SetPlayback(particle, emitter, true, true, true);

        Transform2D transform{};
        TransformSystem2D::Initialize(transform);
        TransformSystem2D::SetLocalPosition(transform, 320.0F, 180.0F);
        TransformSystem2D::UpdateHierarchy(&transform, 1U);

        vr::particle::ParticleRenderer2DCreateInfo renderer_create_info{};
        renderer_create_info.reserve_component_count = 1U;
        renderer_create_info.reserve_particle_count = 256U;
        renderer_create_info.clear_swapchain = true;
        renderer_create_info.clear_color = VkClearColorValue{{0.12F, 0.24F, 0.36F, 1.0F}};
        particle_renderer.Initialize(renderer_create_info);
        renderer_initialized = true;
        runtime.Services().Get<vr::runtime::services::ParticleRenderService>().ConfigureRenderer(
            particle_renderer);
        particle_renderer.SetSceneData(&particle, &emitter, &transform, 1U);

        std::uint32_t submitted_frames = 0U;
        std::uint32_t max_uploaded_instances = 0U;
        std::uint32_t max_draw_calls = 0U;
        std::uint32_t max_indirect_draw_calls = 0U;
        std::uint32_t max_descriptor_binds = 0U;
        std::uint32_t max_descriptor_updates = 0U;
        bool direct_pass_seen = false;
        bool direct_pass_clear_policy_seen = false;
        bool direct_pass_descriptor_bindings_seen = false;

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
            max_uploaded_instances = std::max(max_uploaded_instances,
                                              renderer_stats.uploaded_instance_count);
            max_draw_calls = std::max(max_draw_calls, renderer_stats.draw_call_count);
            max_indirect_draw_calls = std::max(max_indirect_draw_calls,
                                               renderer_stats.indirect_draw_count);
            max_descriptor_binds = std::max(max_descriptor_binds,
                                            renderer_stats.descriptor_set_bind_count);
            max_descriptor_updates = std::max(max_descriptor_updates,
                                              renderer_stats.descriptor_set_update_count);
            const auto& graph_service =
                runtime.Services().Get<vr::runtime::services::RenderGraphRuntimeService>();
            if (const auto* compiled_graph = graph_service.TryGetCompiledGraph();
                compiled_graph != nullptr) {
                if (const auto* direct_pass =
                        vr::test::FindCompiledPassByName(*compiled_graph,
                                                         "particle_renderer_2d_direct");
                    direct_pass != nullptr &&
                    direct_pass->executable &&
                    direct_pass->raster_pass.has_value() &&
                    !direct_pass->raster_pass->color_attachments.empty()) {
                    direct_pass_seen = true;
                    const auto& color_attachment =
                        direct_pass->raster_pass->color_attachments.front();
                    direct_pass_clear_policy_seen =
                        direct_pass_clear_policy_seen ||
                        (color_attachment.load_op ==
                             vr::render_graph::AttachmentLoadOp::clear &&
                         color_attachment.clear_value.red ==
                             renderer_create_info.clear_color.float32[0] &&
                         color_attachment.clear_value.green ==
                             renderer_create_info.clear_color.float32[1] &&
                         color_attachment.clear_value.blue ==
                             renderer_create_info.clear_color.float32[2] &&
                         color_attachment.clear_value.alpha ==
                             renderer_create_info.clear_color.float32[3]);
                    direct_pass_descriptor_bindings_seen =
                        direct_pass_descriptor_bindings_seen ||
                        !direct_pass->descriptor_bindings.empty();
                }
            }
        }

        VR_REQUIRE(submitted_frames > 0U);
        VR_REQUIRE(max_uploaded_instances > 0U);
        VR_REQUIRE(max_draw_calls > 0U);
        VR_REQUIRE(max_descriptor_binds > 0U);
        VR_CHECK(max_descriptor_binds <= max_draw_calls);
        VR_CHECK(max_descriptor_updates == 0U);
        VR_CHECK(direct_pass_seen);
        VR_CHECK(direct_pass_clear_policy_seen);
        VR_CHECK(direct_pass_descriptor_bindings_seen);
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

VR_TEST_CASE(RuntimeIntegration_particle_renderer_2d_gpu_persistent_seed_once,
             "integration;gpu;sdl;runtime;particle;render2d") {
    Runtime runtime{};
    vr::particle::ParticleRenderer2D particle_renderer{};

    bool runtime_initialized = false;
    bool renderer_initialized = false;

    try {
        Runtime::CreateInfo create_info{};
        ConfigureParticle2DRuntimeCreateInfo(create_info, "vr_tests_runtime_particle_2d_gpu");
        runtime.Initialize(create_info);
        runtime_initialized = true;

        Particle2D particle{};
        ParticleEmitter2D emitter{};
        ParticleSystem2D::Initialize(particle);
        ParticleEmitterSystem2D::Initialize(particle, emitter);
        ParticleSystem2D::SetRenderPassHint(particle, vr::ecs::ParticleRenderPassHint::transparent);
        ParticleSystem2D::SetSimulationMode(particle, vr::ecs::ParticleSimulationMode::gpu);
        ParticleSystem2D::SetRenderMode(particle, vr::ecs::ParticleRenderMode::axis_aligned);
        ParticleSystem2D::SetBlendMode(particle, vr::ecs::ParticleBlendMode::premultiplied_alpha);
        ParticleSystem2D::SetPremultipliedAlpha(particle, true);
        ParticleSystem2D::SetStartEndColor(particle,
                                           vr::ecs::Rgba8{255U, 200U, 140U, 255U},
                                           vr::ecs::Rgba8{255U, 64U, 32U, 0U});
        ParticleSystem2D::SetScalarStyle(particle, 22.0F, 0.0F, 1.0F, 0.0F, 0.0F);

        ParticleEmitterSystem2D::SetBurst(particle, emitter, 24U, 0.0F);
        ParticleEmitterSystem2D::SetSpawnRate(particle, emitter, 0.0F);
        ParticleEmitterSystem2D::SetLifetimeRange(particle, emitter, 1.6F, 1.8F);
        ParticleEmitterSystem2D::SetSpeedRange(particle, emitter, 10.0F, 22.0F);
        ParticleEmitterSystem2D::SetSizeRange(particle, emitter, 12.0F, 20.0F, 3.0F, 8.0F);
        ParticleEmitterSystem2D::SetSimulationSpace(particle,
                                                    emitter,
                                                    vr::ecs::ParticleSimulationSpace::world);
        ParticleEmitterSystem2D::SetEmissionShape(particle,
                                                  emitter,
                                                  vr::ecs::ParticleEmitterShape::circle,
                                                  vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                                  6.0F,
                                                  vr::ecs::Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F},
                                                  0.0F,
                                                  0.0F);
        ParticleEmitterSystem2D::SetPlayback(particle, emitter, true, false, true);

        Transform2D transform{};
        TransformSystem2D::Initialize(transform);
        TransformSystem2D::SetLocalPosition(transform, 320.0F, 180.0F);
        TransformSystem2D::UpdateHierarchy(&transform, 1U);

        vr::particle::ParticleRenderer2DCreateInfo renderer_create_info{};
        renderer_create_info.reserve_component_count = 1U;
        renderer_create_info.reserve_particle_count = 256U;
        renderer_create_info.clear_swapchain = false;
        particle_renderer.Initialize(renderer_create_info);
        renderer_initialized = true;
        runtime.Services().Get<vr::runtime::services::ParticleRenderService>().ConfigureRenderer(
            particle_renderer);
        particle_renderer.SetSceneData(&particle, &emitter, &transform, 1U);

        std::uint32_t submitted_frames = 0U;
        std::uint32_t max_indirect_draw_calls = 0U;
        std::uint32_t max_uploaded_instances = 0U;
        std::uint32_t max_draw_calls = 0U;
        std::uint32_t max_descriptor_binds = 0U;
        std::uint32_t max_descriptor_updates = 0U;

        constexpr std::uint32_t max_ticks = 8U;
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
        VR_CHECK(max_descriptor_binds <= max_draw_calls);
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

} // namespace

