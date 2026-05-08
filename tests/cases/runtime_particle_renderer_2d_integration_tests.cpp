#include "support/test_framework.hpp"
#include "vr/ecs/system/particle_emitter_system.hpp"
#include "vr/ecs/system/particle_system.hpp"
#include "vr/ecs/system/transform_system.hpp"
#include "vr/particle/particle_renderer_2d.hpp"
#include "vr/particle/particle_simulation_host.hpp"
#include "vr/particle/particle_upload_host.hpp"
#include "vr/render/render_runtime_host.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace {

using Runtime = vr::render::RenderRuntimeHost<vr::platform::ActiveBackendTag, 2U>;
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
    constexpr std::array<std::string_view, 15U> patterns{
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

struct ParticleRecorder2D final {
    vr::particle::ParticleRenderer2D* renderer = nullptr;

    void PrepareFrame(const vr::render::RuntimePrepareContext& prepare_context_) {
        renderer->PrepareFrame(prepare_context_);
    }

    void Record(const vr::render::FrameRecordContext& record_context_) {
        renderer->Record(record_context_);
    }

    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_,
                              std::uint64_t last_submitted_value_,
                              std::uint64_t completed_submit_value_) {
        renderer->OnSwapchainRecreated(image_count_,
                                       extent_,
                                       format_,
                                       last_submitted_value_,
                                       completed_submit_value_);
    }
};

VR_TEST_CASE(RuntimeIntegration_particle_renderer_2d_hybrid_smoke,
             "integration;gpu;sdl;runtime;particle;render2d") {
    Runtime runtime{};
    vr::particle::ParticleUploadHost particle_upload_host{};
    vr::particle::ParticleSimulationHost particle_simulation_host{};
    vr::particle::ParticleRenderer2D particle_renderer{};

    bool runtime_initialized = false;
    bool upload_initialized = false;
    bool simulation_initialized = false;
    bool renderer_initialized = false;

    try {
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "vr_tests_runtime_particle_2d";
        create_info.platform.window.width = 640;
        create_info.platform.window.height = 360;
        create_info.platform.window.resizable = true;
        create_info.platform.window.high_pixel_density = true;
        create_info.platform.instance.enable_validation = false;
        create_info.platform.device.required_vulkan13_features.dynamicRendering = VK_TRUE;
        create_info.platform.device.required_vulkan13_features.synchronization2 = VK_TRUE;
        create_info.render_loop.swapchain.enable_vsync = false;
        create_info.render_loop.swapchain.preferred_image_count = 2U;
        create_info.render_loop.commands.initial_primary_per_frame = 2U;
        create_info.render_loop.commands.primary_growth_chunk = 2U;
        create_info.poll_events_each_tick = true;
        runtime.Initialize(create_info);
        runtime_initialized = true;

        vr::particle::ParticleUploadHostCreateInfo upload_create_info{};
        upload_create_info.frames_in_flight = 2U;
        upload_create_info.initial_2d_instance_buffer_bytes = 256U * 1024U;
        particle_upload_host.Initialize(runtime.Context(), runtime.GpuMemory(), upload_create_info);
        upload_initialized = true;

        vr::particle::ParticleSimulationHostCreateInfo simulation_create_info{};
        simulation_create_info.frames_in_flight = 2U;
        simulation_create_info.initial_particle_capacity = 1024U;
        simulation_create_info.initial_visible_particle_capacity = 1024U;
        simulation_create_info.initial_spawn_packet_capacity = 64U;
        simulation_create_info.initial_indirect_command_capacity = 32U;
        simulation_create_info.initial_sort_key_capacity = 1024U;
        particle_simulation_host.Initialize(runtime.Context(), runtime.GpuMemory(), simulation_create_info);
        simulation_initialized = true;

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
        renderer_create_info.clear_swapchain = false;
        particle_renderer.Initialize(renderer_create_info);
        renderer_initialized = true;
        particle_renderer.SetHost(&particle_upload_host);
        particle_renderer.SetSimulationHost(&particle_simulation_host);
        particle_renderer.SetSceneData(&particle, &emitter, &transform, 1U);

        ParticleRecorder2D recorder{.renderer = &particle_renderer};

        std::uint32_t submitted_frames = 0U;
        std::uint32_t max_uploaded_instances = 0U;
        std::uint32_t max_draw_calls = 0U;
        std::uint32_t max_indirect_draw_calls = 0U;

        constexpr std::uint32_t max_ticks = 10U;
        for (std::uint32_t tick_index = 0U;
             tick_index < max_ticks && runtime.IsRunning();
             ++tick_index) {
            const auto tick_result = runtime.Tick(recorder);
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
        }

        VR_REQUIRE(submitted_frames > 0U);
        VR_REQUIRE(max_uploaded_instances > 0U);
        VR_REQUIRE(max_draw_calls > 0U);
        VR_REQUIRE(particle_simulation_host.Stats().prepared_frame_count > 0U);
        if (particle_simulation_host.Capabilities().SupportsHybridSimulation()) {
            VR_REQUIRE(particle_simulation_host.Stats().gpu_build_prepare_count > 0U);
            VR_REQUIRE(particle_simulation_host.Stats().gpu_build_dispatch_count > 0U);
            VR_REQUIRE(particle_simulation_host.Stats().update_dispatch_count > 0U);
            VR_REQUIRE(max_indirect_draw_calls > 0U);
        }

        particle_renderer.Shutdown(runtime.Context());
        renderer_initialized = false;
        particle_simulation_host.Shutdown(runtime.Context());
        simulation_initialized = false;
        particle_upload_host.Shutdown(runtime.Context());
        upload_initialized = false;
        runtime.Shutdown();
        runtime_initialized = false;
    } catch (const std::exception& e) {
        if (IsEnvironmentSkipError(e.what())) {
            if (renderer_initialized) {
                particle_renderer.Shutdown(runtime.Context());
                renderer_initialized = false;
            }
            if (simulation_initialized) {
                particle_simulation_host.Shutdown(runtime.Context());
                simulation_initialized = false;
            }
            if (upload_initialized) {
                particle_upload_host.Shutdown(runtime.Context());
                upload_initialized = false;
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
        if (simulation_initialized) {
            particle_simulation_host.Shutdown(runtime.Context());
            simulation_initialized = false;
        }
        if (upload_initialized) {
            particle_upload_host.Shutdown(runtime.Context());
            upload_initialized = false;
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
    vr::particle::ParticleUploadHost particle_upload_host{};
    vr::particle::ParticleSimulationHost particle_simulation_host{};
    vr::particle::ParticleRenderer2D particle_renderer{};

    bool runtime_initialized = false;
    bool upload_initialized = false;
    bool simulation_initialized = false;
    bool renderer_initialized = false;

    try {
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "vr_tests_runtime_particle_2d_gpu";
        create_info.platform.window.width = 640;
        create_info.platform.window.height = 360;
        create_info.platform.window.resizable = true;
        create_info.platform.window.high_pixel_density = true;
        create_info.platform.instance.enable_validation = false;
        create_info.platform.device.required_vulkan13_features.dynamicRendering = VK_TRUE;
        create_info.platform.device.required_vulkan13_features.synchronization2 = VK_TRUE;
        create_info.render_loop.swapchain.enable_vsync = false;
        create_info.render_loop.swapchain.preferred_image_count = 2U;
        create_info.render_loop.commands.initial_primary_per_frame = 2U;
        create_info.render_loop.commands.primary_growth_chunk = 2U;
        create_info.poll_events_each_tick = true;
        runtime.Initialize(create_info);
        runtime_initialized = true;

        vr::particle::ParticleUploadHostCreateInfo upload_create_info{};
        upload_create_info.frames_in_flight = 2U;
        upload_create_info.initial_2d_instance_buffer_bytes = 256U * 1024U;
        particle_upload_host.Initialize(runtime.Context(), runtime.GpuMemory(), upload_create_info);
        upload_initialized = true;

        vr::particle::ParticleSimulationHostCreateInfo simulation_create_info{};
        simulation_create_info.frames_in_flight = 2U;
        simulation_create_info.initial_particle_capacity = 1024U;
        simulation_create_info.initial_visible_particle_capacity = 1024U;
        simulation_create_info.initial_spawn_packet_capacity = 64U;
        simulation_create_info.initial_indirect_command_capacity = 32U;
        simulation_create_info.initial_sort_key_capacity = 1024U;
        particle_simulation_host.Initialize(runtime.Context(), runtime.GpuMemory(), simulation_create_info);
        simulation_initialized = true;

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
        particle_renderer.SetHost(&particle_upload_host);
        particle_renderer.SetSimulationHost(&particle_simulation_host);
        particle_renderer.SetSceneData(&particle, &emitter, &transform, 1U);

        ParticleRecorder2D recorder{.renderer = &particle_renderer};

        std::uint32_t submitted_frames = 0U;
        std::uint32_t max_indirect_draw_calls = 0U;
        std::uint32_t max_uploaded_instances = 0U;

        constexpr std::uint32_t max_ticks = 8U;
        for (std::uint32_t tick_index = 0U;
             tick_index < max_ticks && runtime.IsRunning();
             ++tick_index) {
            const auto tick_result = runtime.Tick(recorder);
            if (tick_result.render.code != vr::render::TickCode::Submitted) {
                continue;
            }
            ++submitted_frames;
            const auto& renderer_stats = particle_renderer.Stats();
            max_indirect_draw_calls = std::max(max_indirect_draw_calls,
                                               renderer_stats.indirect_draw_count);
            max_uploaded_instances = std::max(max_uploaded_instances,
                                              renderer_stats.uploaded_instance_count);
        }

        VR_REQUIRE(submitted_frames > 0U);
        VR_REQUIRE(max_indirect_draw_calls > 0U);
        VR_REQUIRE(max_uploaded_instances > 0U);
        VR_REQUIRE(particle_simulation_host.Stats().prepared_frame_count > 0U);
        if (particle_simulation_host.Capabilities().SupportsGpuSimulation()) {
            VR_REQUIRE(particle_simulation_host.Stats().gpu_build_prepare_count > 0U);
            VR_REQUIRE(particle_simulation_host.Stats().gpu_build_dispatch_count > 0U);
            VR_REQUIRE(particle_simulation_host.Stats().update_dispatch_count > 0U);
            VR_REQUIRE(particle_simulation_host.Stats().state_upload_count > 0U);
            VR_REQUIRE(particle_simulation_host.Stats().state_upload_count <=
                       particle_simulation_host.FramesInFlight());
            VR_REQUIRE(particle_simulation_host.Stats().spawn_packet_upload_count <=
                       particle_simulation_host.FramesInFlight());
            VR_REQUIRE(particle_renderer.Stats().cache_reused);
        }

        particle_renderer.Shutdown(runtime.Context());
        renderer_initialized = false;
        particle_simulation_host.Shutdown(runtime.Context());
        simulation_initialized = false;
        particle_upload_host.Shutdown(runtime.Context());
        upload_initialized = false;
        runtime.Shutdown();
        runtime_initialized = false;
    } catch (const std::exception& e) {
        if (IsEnvironmentSkipError(e.what())) {
            if (renderer_initialized) {
                particle_renderer.Shutdown(runtime.Context());
                renderer_initialized = false;
            }
            if (simulation_initialized) {
                particle_simulation_host.Shutdown(runtime.Context());
                simulation_initialized = false;
            }
            if (upload_initialized) {
                particle_upload_host.Shutdown(runtime.Context());
                upload_initialized = false;
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
        if (simulation_initialized) {
            particle_simulation_host.Shutdown(runtime.Context());
            simulation_initialized = false;
        }
        if (upload_initialized) {
            particle_upload_host.Shutdown(runtime.Context());
            upload_initialized = false;
        }
        if (runtime_initialized) {
            runtime.Shutdown();
            runtime_initialized = false;
        }
        throw;
    }
}

} // namespace
