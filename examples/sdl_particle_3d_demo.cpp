#include "vr/ecs/system/bounds_system.hpp"
#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/particle_emitter_system.hpp"
#include "vr/ecs/system/particle_system.hpp"
#include "vr/ecs/system/transform_system.hpp"
#include "vr/particle/particle_renderer_3d.hpp"
#include "vr/particle/particle_simulation_host.hpp"
#include "vr/particle/particle_upload_host.hpp"
#include "vr/render/render_runtime_host.hpp"
#include "vr/render/render_view_submission_utils.hpp"
#include "vr/runtime/crash_tracer_support.hpp"
#include "vr/render/scene_recorder_3d.hpp"

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

[[nodiscard]] vr::render::SceneRecorder3DCreateInfo BuildParticle3DRecorderCreateInfo() noexcept {
    vr::render::SceneRecorder3DCreateInfo create_info{};
    create_info.scene_target.color_debug_name = "Particle3DSceneColor";
    create_info.scene_target.depth_debug_name = "Particle3DSceneDepth";
    create_info.scene_target.enable_depth = true;
    create_info.scene_target.color_lifetime = vr::render::RenderTargetLifetime::transient;
    create_info.scene_target.depth_lifetime = vr::render::RenderTargetLifetime::transient;
    create_info.scene_target.clear_color = VkClearColorValue{{0.035F, 0.04F, 0.06F, 1.0F}};
    create_info.bloom.clear_swapchain = true;
    create_info.bloom.clear_color = {{0.015F, 0.018F, 0.028F, 1.0F}};
    create_info.bloom.enable_reinhard_tonemap = true;
    create_info.bloom.exposure = 1.0F;
    create_info.bloom.apply_manual_gamma = false;
    create_info.bloom.bloom_threshold = 0.72F;
    create_info.bloom.bloom_knee = 0.45F;
    create_info.bloom.bloom_intensity = 0.85F;
    create_info.reserve_scene_renderer_count = 1U;
    create_info.reserve_overlay_renderer_count = 0U;
    return create_info;
}

void InitializeSmokeEmitter(Particle3D& particle_,
                            ParticleEmitter3D& emitter_) {
    ParticleSystem3D::Initialize(particle_);
    ParticleEmitterSystem3D::Initialize(particle_, emitter_);
    ParticleSystem3D::SetRenderPassHint(particle_, vr::ecs::ParticleRenderPassHint::transparent);
    ParticleSystem3D::SetSimulationMode(particle_, vr::ecs::ParticleSimulationMode::hybrid_gpu);
    ParticleSystem3D::SetSortMode(particle_, vr::ecs::ParticleSortMode::gpu_radix);
    ParticleSystem3D::SetRenderMode(particle_, vr::ecs::ParticleRenderMode::billboard);
    ParticleSystem3D::SetFacingMode(particle_, vr::ecs::ParticleFacingMode::screen);
    ParticleSystem3D::SetBlendMode(particle_, vr::ecs::ParticleBlendMode::premultiplied_alpha);
    ParticleSystem3D::SetPremultipliedAlpha(particle_, true);
    ParticleSystem3D::SetStartEndColor(particle_,
                                       vr::ecs::Rgba8{255U, 225U, 195U, 180U},
                                       vr::ecs::Rgba8{52U, 64U, 82U, 0U});
    ParticleSystem3D::SetScalarStyle(particle_, 0.26F, 0.10F, 1.0F, 0.0F, 0.0F);
    ParticleSystem3D::SetDepthState(particle_, true, false);

    ParticleEmitterSystem3D::SetBurst(particle_, emitter_, 32U, 0.0F);
    ParticleEmitterSystem3D::SetSpawnRate(particle_, emitter_, 72.0F);
    ParticleEmitterSystem3D::SetLifetimeRange(particle_, emitter_, 1.0F, 1.7F);
    ParticleEmitterSystem3D::SetSpeedRange(particle_, emitter_, 0.4F, 1.3F);
    ParticleEmitterSystem3D::SetSizeRange(particle_, emitter_, 0.22F, 0.34F, 0.08F, 0.24F);
    ParticleEmitterSystem3D::SetRotationRange(particle_, emitter_, -0.6F, 0.6F, -0.9F, 0.9F);
    ParticleEmitterSystem3D::SetIntegrator(particle_, emitter_, 0.18F, -0.04F);
    ParticleEmitterSystem3D::SetEmissionShape(particle_,
                                              emitter_,
                                              vr::ecs::ParticleEmitterShape::sphere,
                                              vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                              0.18F,
                                              vr::ecs::Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F},
                                              0.45F,
                                              0.55F);
    ParticleEmitterSystem3D::SetPlayback(particle_, emitter_, true, true, true);
}

void InitializeSparkEmitter(Particle3D& particle_,
                            ParticleEmitter3D& emitter_) {
    ParticleSystem3D::Initialize(particle_);
    ParticleEmitterSystem3D::Initialize(particle_, emitter_);
    ParticleSystem3D::SetRenderPassHint(particle_, vr::ecs::ParticleRenderPassHint::transparent);
    ParticleSystem3D::SetSimulationMode(particle_, vr::ecs::ParticleSimulationMode::hybrid_gpu);
    ParticleSystem3D::SetRenderMode(particle_, vr::ecs::ParticleRenderMode::billboard);
    ParticleSystem3D::SetFacingMode(particle_, vr::ecs::ParticleFacingMode::velocity);
    ParticleSystem3D::SetBlendMode(particle_, vr::ecs::ParticleBlendMode::additive);
    ParticleSystem3D::SetStartEndColor(particle_,
                                       vr::ecs::Rgba8{255U, 250U, 230U, 255U},
                                       vr::ecs::Rgba8{255U, 108U, 28U, 0U});
    ParticleSystem3D::SetScalarStyle(particle_, 0.025F, 0.0F, 1.0F, 0.0F, 0.0F);
    ParticleSystem3D::SetDepthState(particle_, true, false);

    ParticleEmitterSystem3D::SetBurst(particle_, emitter_, 40U, 0.22F);
    ParticleEmitterSystem3D::SetSpawnRate(particle_, emitter_, 120.0F);
    ParticleEmitterSystem3D::SetLifetimeRange(particle_, emitter_, 0.35F, 0.75F);
    ParticleEmitterSystem3D::SetSpeedRange(particle_, emitter_, 1.5F, 4.4F);
    ParticleEmitterSystem3D::SetSizeRange(particle_, emitter_, 0.06F, 0.11F, 0.01F, 0.04F);
    ParticleEmitterSystem3D::SetRotationRange(particle_, emitter_, -0.3F, 0.3F, -7.0F, 7.0F);
    ParticleEmitterSystem3D::SetIntegrator(particle_, emitter_, 0.08F, 0.10F);
    ParticleEmitterSystem3D::SetEmissionShape(particle_,
                                              emitter_,
                                              vr::ecs::ParticleEmitterShape::cone,
                                              vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                              0.0F,
                                              vr::ecs::Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F},
                                              0.50F,
                                              0.0F);
    ParticleEmitterSystem3D::SetPlayback(particle_, emitter_, true, true, false);
}

} // namespace

int main(int argc_,
         char** argv_) {
    vr::runtime::InstallProcessCrashTracer(argc_, argv_);
    Runtime runtime{};
    vr::render::SceneRecorder3D recorder{};
    vr::particle::ParticleRenderer3D particle_renderer{};
    const std::uint32_t max_frames = ParseMaxFrames(argc_, argv_);

    bool runtime_initialized = false;
    bool renderer_initialized = false;

    try {
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "Vulkan SDL3 Particle 3D Demo";
        create_info.platform.window.width = 1366;
        create_info.platform.window.height = 768;
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

        recorder.Initialize(BuildParticle3DRecorderCreateInfo());
        recorder.BindRuntime(runtime);

        std::array<Particle3D, 2U> particles{};
        std::array<ParticleEmitter3D, 2U> emitters{};
        InitializeSmokeEmitter(particles[0U], emitters[0U]);
        InitializeSparkEmitter(particles[1U], emitters[1U]);

        std::array<Transform3D, 2U> transforms{};
        std::array<Bounds3D, 2U> bounds{};
        for (std::size_t index = 0; index < transforms.size(); ++index) {
            TransformSystem3D::Initialize(transforms[index]);
            BoundsSystem3D::Initialize(bounds[index]);
        }
        TransformSystem3D::SetLocalPosition(transforms[0U],
                                            vr::ecs::Float3{.x = -0.65F, .y = -0.15F, .z = 0.0F});
        TransformSystem3D::SetLocalPosition(transforms[1U],
                                            vr::ecs::Float3{.x = 0.75F, .y = -0.20F, .z = -0.15F});
        for (auto& bounds_component : bounds) {
            BoundsSystem3D::SetLocalCenterExtents(bounds_component,
                                                  vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                                  vr::ecs::Float3{.x = 0.9F, .y = 0.9F, .z = 0.9F});
        }
        TransformSystem3D::UpdateHierarchy(transforms.data(),
                                           static_cast<std::uint32_t>(transforms.size()));
        (void)BoundsSystem3D::UpdateAligned(bounds.data(),
                                            transforms.data(),
                                            static_cast<std::uint32_t>(bounds.size()));

        Camera3D camera{};
        Transform3D camera_transform{};
        CameraSystem3D::Initialize(camera);
        TransformSystem3D::Initialize(camera_transform);
        CameraSystem3D::SetProjectionMode(camera, vr::ecs::CameraProjectionMode::perspective);
        CameraSystem3D::SetAspectRatio(camera, 1366.0F / 768.0F);
        CameraSystem3D::SetNearFar(camera, 0.05F, 128.0F);
        CameraSystem3D::SetVerticalFovRadians(camera, 60.0F * 0.01745329251994329577F);
        TransformSystem3D::SetLocalPosition(camera_transform,
                                            vr::ecs::Float3{.x = 0.0F, .y = 0.6F, .z = 4.2F});
        TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
        CameraSystem3D::MarkViewDirty(camera);
        CameraSystem3D::Update(camera, camera_transform);

        vr::particle::ParticleRenderer3DCreateInfo renderer_create_info{};
        renderer_create_info.reserve_component_count =
            static_cast<std::uint32_t>(particles.size());
        renderer_create_info.reserve_particle_count = 8192U;
        renderer_create_info.enable_depth = true;
        renderer_create_info.clear_depth = true;
        renderer_create_info.clear_swapchain = false;
        particle_renderer.Initialize(renderer_create_info);
        renderer_initialized = true;
        runtime.Services().Get<vr::runtime::services::ParticleRenderService>().ConfigureRenderer(
            particle_renderer);
        particle_renderer.SetSceneData(particles.data(),
                                       emitters.data(),
                                       transforms.data(),
                                       static_cast<std::uint32_t>(particles.size()),
                                       &camera,
                                       &camera_transform,
                                       bounds.data());
        recorder.RegisterTransparentSceneRenderer(particle_renderer,
                                                  vr::render::SceneRenderPassRole::single);

        vr::render::RenderView3D main_view{};
        vr::render::RenderScenePacket3D main_scene_packet{};
        vr::render::RefreshExtentBoundWorldSceneSubmission(main_view,
                                                           main_scene_packet,
                                                           camera,
                                                           camera_transform,
                                                           runtime.Swapchain().Extent(),
                                                           0U);
        recorder.SetFramePacket(&main_scene_packet);

        std::cout << "sdl_particle_3d_demo running (hybrid particle 3D + SceneRecorder3D). Close window to exit.\n";

        std::uint32_t frame_counter = 0U;
        std::uint64_t stats_last_tick = SDL_GetTicks();
        std::uint32_t stats_frame_counter = 0U;

        while (runtime.IsRunning()) {
            const float time_seconds = static_cast<float>(SDL_GetTicks()) * 0.001F;
            TransformSystem3D::SetLocalPosition(transforms[0U],
                                                vr::ecs::Float3{
                                                    .x = -0.65F + 0.18F * std::sin(time_seconds * 0.8F),
                                                    .y = -0.15F,
                                                    .z = 0.10F * std::cos(time_seconds * 1.1F)});
            TransformSystem3D::SetLocalPosition(transforms[1U],
                                                vr::ecs::Float3{
                                                    .x = 0.75F + 0.20F * std::cos(time_seconds * 1.2F),
                                                    .y = -0.20F,
                                                    .z = -0.15F + 0.12F * std::sin(time_seconds * 1.7F)});
            TransformSystem3D::UpdateHierarchy(transforms.data(),
                                               static_cast<std::uint32_t>(transforms.size()));
            (void)BoundsSystem3D::UpdateAligned(bounds.data(),
                                                transforms.data(),
                                                static_cast<std::uint32_t>(bounds.size()));

            const float orbit_angle = time_seconds * 0.35F;
            TransformSystem3D::SetLocalPosition(camera_transform,
                                                vr::ecs::Float3{
                                                    .x = std::sin(orbit_angle) * 4.0F,
                                                    .y = 0.8F + 0.15F * std::sin(time_seconds * 0.7F),
                                                    .z = std::cos(orbit_angle) * 4.0F});
            TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
            CameraSystem3D::MarkViewDirty(camera);
            CameraSystem3D::Update(camera, camera_transform);
            vr::render::RefreshExtentBoundWorldSceneSubmission(main_view,
                                                               main_scene_packet,
                                                               camera,
                                                               camera_transform,
                                                               runtime.Swapchain().Extent(),
                                                               frame_counter);

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
                const auto& sim_stats =
                    runtime.Services().Get<vr::runtime::services::ParticleSimulationService>().Stats();
                std::cout << "FPS:" << static_cast<int>(std::round(fps))
                          << " Frame:" << frame_counter
                          << " Draw:" << renderer_stats.draw_call_count
                          << " Indirect:" << renderer_stats.indirect_draw_count
                          << " Transparent:" << renderer_stats.transparent_draw_call_count
                          << " Visible:" << renderer_stats.visible_particle_count
                          << " Culled:" << renderer_stats.culling_culled_count
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
        runtime.Shutdown();
        runtime_initialized = false;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "sdl_particle_3d_demo failed: " << e.what() << '\n';
        if (renderer_initialized) {
            particle_renderer.Shutdown(runtime.Context());
        }
        if (runtime_initialized) {
            runtime.Shutdown();
        }
        return 1;
    }
}

