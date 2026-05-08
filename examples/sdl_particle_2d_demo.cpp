#include "vr/ecs/system/particle_emitter_system.hpp"
#include "vr/ecs/system/particle_system.hpp"
#include "vr/ecs/system/transform_system.hpp"
#include "vr/particle/particle_renderer_2d.hpp"
#include "vr/particle/particle_simulation_host.hpp"
#include "vr/particle/particle_upload_host.hpp"
#include "vr/render/render_runtime_host.hpp"
#include "vr/render/render_view_submission_utils.hpp"
#include "vr/render/scene_recorder_2d.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

using Runtime = vr::render::RenderRuntimeHost<vr::platform::ActiveBackendTag, 2U>;
using Particle2D = vr::ecs::Particle<vr::ecs::Dim2>;
using ParticleEmitter2D = vr::ecs::ParticleEmitter<vr::ecs::Dim2>;
using Transform2D = vr::ecs::Transform<vr::ecs::Dim2>;
using ParticleSystem2D = vr::ecs::ParticleSystem<vr::ecs::Dim2>;
using ParticleEmitterSystem2D = vr::ecs::ParticleEmitterSystem<vr::ecs::Dim2>;
using TransformSystem2D = vr::ecs::TransformSystem<vr::ecs::Dim2>;

[[nodiscard]] std::uint32_t ParseMaxFrames(int argc_,
                                           char** argv_) {
    if (argc_ <= 1 || argv_ == nullptr) {
        return 0U;
    }

    for (int index = 1; index + 1 < argc_; ++index) {
        if (std::string_view(argv_[index]) != "--frames") {
            continue;
        }
        return static_cast<std::uint32_t>(std::strtoul(argv_[index + 1], nullptr, 10));
    }
    return 0U;
}

[[nodiscard]] vr::render::SceneRecorder2DCreateInfo BuildParticle2DRecorderCreateInfo() noexcept {
    vr::render::SceneRecorder2DCreateInfo create_info{};
    create_info.scene_target.color_debug_name = "Particle2DSceneColor";
    create_info.scene_target.enable_depth = false;
    create_info.scene_target.color_lifetime = vr::render::RenderTargetLifetime::transient;
    create_info.scene_target.clear_color = VkClearColorValue{{0.045F, 0.05F, 0.08F, 1.0F}};
    create_info.reserve_scene_renderer_count = 1U;
    create_info.reserve_overlay_renderer_count = 0U;
    return create_info;
}

void InitializeSmokeEmitter(Particle2D& particle_,
                            ParticleEmitter2D& emitter_) {
    ParticleSystem2D::Initialize(particle_);
    ParticleEmitterSystem2D::Initialize(particle_, emitter_);
    ParticleSystem2D::SetRenderPassHint(particle_, vr::ecs::ParticleRenderPassHint::transparent);
    ParticleSystem2D::SetSimulationMode(particle_, vr::ecs::ParticleSimulationMode::hybrid_gpu);
    ParticleSystem2D::SetRenderMode(particle_, vr::ecs::ParticleRenderMode::axis_aligned);
    ParticleSystem2D::SetFacingMode(particle_, vr::ecs::ParticleFacingMode::screen);
    ParticleSystem2D::SetBlendMode(particle_, vr::ecs::ParticleBlendMode::premultiplied_alpha);
    ParticleSystem2D::SetPremultipliedAlpha(particle_, true);
    ParticleSystem2D::SetLayer(particle_, 12);
    ParticleSystem2D::SetStartEndColor(particle_,
                                       vr::ecs::Rgba8{255U, 214U, 170U, 210U},
                                       vr::ecs::Rgba8{42U, 58U, 74U, 0U});
    ParticleSystem2D::SetScalarStyle(particle_, 28.0F, 0.0F, 1.0F, 0.0F, 0.0F);

    ParticleEmitterSystem2D::SetBurst(particle_, emitter_, 24U, 0.0F);
    ParticleEmitterSystem2D::SetSpawnRate(particle_, emitter_, 64.0F);
    ParticleEmitterSystem2D::SetLifetimeRange(particle_, emitter_, 0.9F, 1.6F);
    ParticleEmitterSystem2D::SetSpeedRange(particle_, emitter_, 18.0F, 44.0F);
    ParticleEmitterSystem2D::SetSizeRange(particle_, emitter_, 18.0F, 34.0F, 8.0F, 28.0F);
    ParticleEmitterSystem2D::SetRotationRange(particle_, emitter_, -0.9F, 0.9F, -0.8F, 0.8F);
    ParticleEmitterSystem2D::SetIntegrator(particle_, emitter_, 0.34F, -0.06F);
    ParticleEmitterSystem2D::SetEmissionShape(particle_,
                                              emitter_,
                                              vr::ecs::ParticleEmitterShape::circle,
                                              vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                              14.0F,
                                              vr::ecs::Float3{.x = 0.0F, .y = -1.0F, .z = 0.0F},
                                              0.0F,
                                              0.65F);
    ParticleEmitterSystem2D::SetPlayback(particle_, emitter_, true, true, true);
}

void InitializeSparkEmitter(Particle2D& particle_,
                            ParticleEmitter2D& emitter_) {
    ParticleSystem2D::Initialize(particle_);
    ParticleEmitterSystem2D::Initialize(particle_, emitter_);
    ParticleSystem2D::SetRenderPassHint(particle_, vr::ecs::ParticleRenderPassHint::transparent);
    ParticleSystem2D::SetSimulationMode(particle_, vr::ecs::ParticleSimulationMode::hybrid_gpu);
    ParticleSystem2D::SetRenderMode(particle_, vr::ecs::ParticleRenderMode::axis_aligned);
    ParticleSystem2D::SetFacingMode(particle_, vr::ecs::ParticleFacingMode::screen);
    ParticleSystem2D::SetBlendMode(particle_, vr::ecs::ParticleBlendMode::additive);
    ParticleSystem2D::SetLayer(particle_, 18);
    ParticleSystem2D::SetStartEndColor(particle_,
                                       vr::ecs::Rgba8{255U, 248U, 210U, 255U},
                                       vr::ecs::Rgba8{255U, 120U, 32U, 0U});
    ParticleSystem2D::SetScalarStyle(particle_, 10.0F, 0.0F, 1.0F, 0.0F, 0.0F);

    ParticleEmitterSystem2D::SetBurst(particle_, emitter_, 32U, 0.18F);
    ParticleEmitterSystem2D::SetSpawnRate(particle_, emitter_, 88.0F);
    ParticleEmitterSystem2D::SetLifetimeRange(particle_, emitter_, 0.28F, 0.55F);
    ParticleEmitterSystem2D::SetSpeedRange(particle_, emitter_, 80.0F, 180.0F);
    ParticleEmitterSystem2D::SetSizeRange(particle_, emitter_, 5.0F, 9.0F, 1.0F, 3.0F);
    ParticleEmitterSystem2D::SetRotationRange(particle_, emitter_, -0.25F, 0.25F, -5.0F, 5.0F);
    ParticleEmitterSystem2D::SetIntegrator(particle_, emitter_, 0.12F, 0.08F);
    ParticleEmitterSystem2D::SetEmissionShape(particle_,
                                              emitter_,
                                              vr::ecs::ParticleEmitterShape::point,
                                              vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                              0.0F,
                                              vr::ecs::Float3{.x = 0.0F, .y = -1.0F, .z = 0.0F},
                                              0.0F,
                                              0.55F);
    ParticleEmitterSystem2D::SetPlayback(particle_, emitter_, true, true, true);
}

} // namespace

int main(int argc_,
         char** argv_) {
    Runtime runtime{};
    vr::particle::ParticleUploadHost particle_upload_host{};
    vr::particle::ParticleSimulationHost particle_simulation_host{};
    vr::render::SceneRecorder2D recorder{};
    vr::particle::ParticleRenderer2D particle_renderer{};
    const std::uint32_t max_frames = ParseMaxFrames(argc_, argv_);

    bool runtime_initialized = false;
    bool upload_initialized = false;
    bool simulation_initialized = false;
    bool renderer_initialized = false;

    try {
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "Vulkan SDL3 Particle 2D Demo";
        create_info.platform.window.width = 1280;
        create_info.platform.window.height = 720;
        create_info.platform.window.resizable = true;
        create_info.platform.window.high_pixel_density = true;
        create_info.platform.instance.enable_validation = true;
        create_info.platform.device.required_vulkan13_features.dynamicRendering = VK_TRUE;
        create_info.platform.device.required_vulkan13_features.synchronization2 = VK_TRUE;
        create_info.render_loop.swapchain.enable_vsync = true;
        create_info.render_loop.swapchain.preferred_image_count = 3U;
        create_info.render_loop.commands.initial_primary_per_frame = 2U;
        create_info.render_loop.commands.primary_growth_chunk = 2U;
        create_info.poll_events_each_tick = true;
        runtime.Initialize(create_info);
        runtime_initialized = true;

        recorder.Initialize(BuildParticle2DRecorderCreateInfo());
        recorder.BindRuntime(runtime);

        vr::particle::ParticleUploadHostCreateInfo upload_create_info{};
        upload_create_info.frames_in_flight = 2U;
        upload_create_info.initial_2d_instance_buffer_bytes = 512U * 1024U;
        particle_upload_host.Initialize(runtime.Context(), runtime.GpuMemory(), upload_create_info);
        upload_initialized = true;

        vr::particle::ParticleSimulationHostCreateInfo simulation_create_info{};
        simulation_create_info.frames_in_flight = 2U;
        simulation_create_info.initial_particle_capacity = 4096U;
        simulation_create_info.initial_visible_particle_capacity = 4096U;
        simulation_create_info.initial_spawn_packet_capacity = 128U;
        simulation_create_info.initial_indirect_command_capacity = 64U;
        simulation_create_info.initial_sort_key_capacity = 4096U;
        particle_simulation_host.Initialize(runtime.Context(), runtime.GpuMemory(), simulation_create_info);
        simulation_initialized = true;

        std::array<Particle2D, 2U> particles{};
        std::array<ParticleEmitter2D, 2U> emitters{};
        InitializeSmokeEmitter(particles[0U], emitters[0U]);
        InitializeSparkEmitter(particles[1U], emitters[1U]);

        std::array<Transform2D, 2U> transforms{};
        for (auto& transform : transforms) {
            TransformSystem2D::Initialize(transform);
        }
        TransformSystem2D::SetLocalPosition(transforms[0U], 420.0F, 450.0F);
        TransformSystem2D::SetLocalPosition(transforms[1U], 860.0F, 470.0F);
        TransformSystem2D::UpdateHierarchy(transforms.data(),
                                           static_cast<std::uint32_t>(transforms.size()));

        vr::particle::ParticleRenderer2DCreateInfo renderer_create_info{};
        renderer_create_info.reserve_component_count =
            static_cast<std::uint32_t>(particles.size());
        renderer_create_info.reserve_particle_count = 4096U;
        renderer_create_info.input_positions_pixel_space = true;
        renderer_create_info.pixel_space_origin_top_left = true;
        renderer_create_info.clear_swapchain = false;
        particle_renderer.Initialize(renderer_create_info);
        renderer_initialized = true;
        particle_renderer.SetHost(&particle_upload_host);
        particle_renderer.SetSimulationHost(&particle_simulation_host);
        particle_renderer.SetSceneData(particles.data(),
                                       emitters.data(),
                                       transforms.data(),
                                       static_cast<std::uint32_t>(particles.size()));
        recorder.RegisterSceneRenderer(particle_renderer, vr::render::SceneRenderPassRole::single);

        vr::render::RenderView2D main_view{};
        vr::render::RenderScenePacket2D main_scene_packet{};
        vr::render::RefreshExtentBoundScreenSceneSubmission(main_view,
                                                            main_scene_packet,
                                                            runtime.Swapchain().Extent(),
                                                            0U,
                                                            vr::render::RenderViewKind::world,
                                                            vr::render::render_view_overlay_enabled_flag,
                                                            vr::render::render_scene_packet_allow_overlay_flag);
        recorder.SetFramePacket(&main_scene_packet);

        std::cout << "sdl_particle_2d_demo running (hybrid particle 2D + SceneRecorder2D). Close window to exit.\n";

        std::uint32_t frame_counter = 0U;
        std::uint64_t stats_last_tick = SDL_GetTicks();
        std::uint32_t stats_frame_counter = 0U;

        while (runtime.IsRunning()) {
            const float time_seconds = static_cast<float>(SDL_GetTicks()) * 0.001F;
            TransformSystem2D::SetLocalPosition(transforms[0U],
                                                420.0F + 32.0F * std::sin(time_seconds * 0.9F),
                                                450.0F + 18.0F * std::sin(time_seconds * 1.4F));
            TransformSystem2D::SetLocalPosition(transforms[1U],
                                                860.0F + 60.0F * std::cos(time_seconds * 1.1F),
                                                470.0F + 24.0F * std::sin(time_seconds * 1.9F));
            TransformSystem2D::UpdateHierarchy(transforms.data(),
                                               static_cast<std::uint32_t>(transforms.size()));

            vr::render::RefreshExtentBoundScreenSceneSubmission(main_view,
                                                                main_scene_packet,
                                                                runtime.Swapchain().Extent(),
                                                                frame_counter,
                                                                vr::render::RenderViewKind::world,
                                                                vr::render::render_view_overlay_enabled_flag,
                                                                vr::render::render_scene_packet_allow_overlay_flag);

            const auto tick_result = runtime.Tick(recorder);
            if (tick_result.render.code == vr::render::TickCode::Submitted) {
                ++frame_counter;
                ++stats_frame_counter;
            }

            const std::uint64_t now_ticks = SDL_GetTicks();
            if (now_ticks - stats_last_tick >= 1000U) {
                const double seconds = static_cast<double>(now_ticks - stats_last_tick) / 1000.0;
                const double fps = seconds > 0.0
                    ? static_cast<double>(stats_frame_counter) / seconds
                    : 0.0;
                stats_last_tick = now_ticks;
                stats_frame_counter = 0U;

                const auto& renderer_stats = particle_renderer.Stats();
                const auto& sim_stats = particle_simulation_host.Stats();
                std::cout << "FPS:" << static_cast<int>(std::round(fps))
                          << " Frame:" << frame_counter
                          << " Draw:" << renderer_stats.draw_call_count
                          << " Indirect:" << renderer_stats.indirect_draw_count
                          << " Batch:" << renderer_stats.draw_batch_count
                          << " Visible:" << renderer_stats.visible_particle_count
                          << " UploadBytes:" << renderer_stats.uploaded_bytes
                          << " SimPrep:" << sim_stats.gpu_build_prepare_count
                          << " SimDispatch:" << sim_stats.gpu_build_dispatch_count
                          << '\n';
            }

            if (max_frames > 0U && frame_counter >= max_frames) {
                break;
            }
        }

        particle_renderer.Shutdown(runtime.Context());
        renderer_initialized = false;
        particle_simulation_host.Shutdown(runtime.Context());
        simulation_initialized = false;
        particle_upload_host.Shutdown(runtime.Context());
        upload_initialized = false;
        runtime.Shutdown();
        runtime_initialized = false;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "sdl_particle_2d_demo failed: " << e.what() << '\n';
        if (renderer_initialized) {
            particle_renderer.Shutdown(runtime.Context());
        }
        if (simulation_initialized) {
            particle_simulation_host.Shutdown(runtime.Context());
        }
        if (upload_initialized) {
            particle_upload_host.Shutdown(runtime.Context());
        }
        if (runtime_initialized) {
            runtime.Shutdown();
        }
        return 1;
    }
}
