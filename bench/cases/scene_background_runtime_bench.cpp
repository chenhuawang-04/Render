#include "support/bench_framework.hpp"
#include "vr/render/environment/sky_environment_gpu_host.hpp"
#include "vr/render/pipeline_host.hpp"
#include "vr/render/render_target_host.hpp"
#include "vr/render/render_view.hpp"
#include "vr/render/scene_recorder_3d.hpp"
#include "vr/render/scene_submission.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/resource/sampler_host.hpp"
#include "vr/scene/scene.hpp"
#include "vr/scene/scene_prepare.hpp"

#include <cstdint>
#include <stdexcept>

namespace {

VR_BENCHMARK_CASE(ScenePrepare_dim2_resolve_background_steady_state,
                  "core;scene;background;prepare;cpu") {
    vr::scene::Scene2D scene{};
    scene.background.mode = vr::scene::Background2DMode::gradient;
    scene.background.color0 = vr::ecs::Float4{.x = 0.12F, .y = 0.18F, .z = 0.24F, .w = 1.0F};
    scene.background.color1 = vr::ecs::Float4{.x = 0.82F, .y = 0.78F, .z = 0.64F, .w = 1.0F};
    scene.background.opacity = 0.95F;
    scene.background.layer = -32000;
    scene.background.revision = 17U;

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t iteration = 0U; iteration < iterations; ++iteration) {
        const vr::scene::Background2DRenderState state =
            vr::scene::ScenePrepare<vr::ecs::Dim2, vr::scene::SpriteBackground>::Resolve(scene);
        vr::bench::BenchmarkContext::DoNotOptimize(state.mode);
        vr::bench::BenchmarkContext::DoNotOptimize(state.revision);
        vr::bench::BenchmarkContext::DoNotOptimize(state.opacity);
    }

    bench_context_.AddItems(iterations);
    bench_context_.AddBytes(iterations * sizeof(vr::scene::Background2DRenderState));
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(ScenePrepare_dim3_resolve_environment_steady_state,
                  "core;scene;background;prepare;cpu") {
    vr::scene::Scene3D scene{};
    scene.background.mode = vr::scene::SkyEnvironmentMode::cubemap;
    scene.background.sky_texture_id = 7U;
    scene.background.irradiance_texture_id = 11U;
    scene.background.prefiltered_texture_id = 12U;
    scene.background.brdf_lut_texture_id = 13U;
    scene.background.zenith_color = vr::ecs::Float4{.x = 0.08F, .y = 0.12F, .z = 0.20F, .w = 1.0F};
    scene.background.horizon_color = vr::ecs::Float4{.x = 0.38F, .y = 0.28F, .z = 0.18F, .w = 1.0F};
    scene.background.ground_color = vr::ecs::Float4{.x = 0.03F, .y = 0.03F, .z = 0.04F, .w = 1.0F};
    scene.background.tint = vr::ecs::Float4{.x = 1.0F, .y = 0.96F, .z = 0.92F, .w = 1.0F};
    scene.background.exposure = 1.0F;
    scene.background.sky_intensity = 1.15F;
    scene.background.diffuse_ibl_intensity = 1.10F;
    scene.background.specular_ibl_intensity = 1.05F;
    scene.background.rotation_y = 0.35F;
    scene.background.max_specular_lod = 5.0F;
    scene.background.revision = 23U;

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t iteration = 0U; iteration < iterations; ++iteration) {
        const vr::scene::SkyEnvironmentRenderState state =
            vr::scene::ScenePrepare<vr::ecs::Dim3, vr::scene::SkyEnvironment>::Resolve(scene);
        vr::bench::BenchmarkContext::DoNotOptimize(state.mode);
        vr::bench::BenchmarkContext::DoNotOptimize(state.revision);
        vr::bench::BenchmarkContext::DoNotOptimize(state.prefiltered_texture_id);
    }

    bench_context_.AddItems(iterations);
    bench_context_.AddBytes(iterations * sizeof(vr::scene::SkyEnvironmentRenderState));
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(SkyEnvironmentGpuHost_register_or_update_static_cache_hit,
                  "core;scene;background;gpu_host;cpu") {
    vr::VulkanContext context{};
    vr::asset::TextureHost texture_host{};
    vr::render::DescriptorHost descriptor_host{};
    vr::resource::SamplerHost sampler_host{};
    vr::render::UploadHost upload_host{};
    vr::render::SkyEnvironmentGpuHost host{};
    host.Initialize(context, texture_host, descriptor_host, sampler_host, {});

    vr::render::SkyEnvironmentGpuPrepareView prepare_view{
        .device = context,
        .texture = texture_host,
        .upload = upload_host,
        .descriptor = descriptor_host,
        .sampler = sampler_host,
    };

    vr::scene::SkyEnvironmentRenderState state{};
    state.mode = vr::scene::SkyEnvironmentMode::cubemap;
    state.sky_texture_id = 41U;
    state.irradiance_texture_id = 101U;
    state.prefiltered_texture_id = 102U;
    state.brdf_lut_texture_id = 103U;
    state.tint = vr::ecs::Float4{.x = 1.0F, .y = 0.95F, .z = 0.9F, .w = 1.0F};
    state.exposure = 1.0F;
    state.sky_intensity = 1.0F;
    state.diffuse_ibl_intensity = 1.0F;
    state.specular_ibl_intensity = 1.0F;
    state.max_specular_lod = 6.0F;
    state.revision = 29U;

    const vr::scene::SkyEnvironmentGpuHandle warmup_handle = host.RegisterOrUpdate(state, prepare_view);
    if (!warmup_handle.IsValid()) {
        throw std::runtime_error("SkyEnvironmentGpuHost warmup handle is invalid.");
    }

    const std::uint32_t initial_cache_hits = host.Stats().cache_hit_count;
    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t iteration = 0U; iteration < iterations; ++iteration) {
        const vr::scene::SkyEnvironmentGpuHandle handle = host.RegisterOrUpdate(state, prepare_view);
        vr::bench::BenchmarkContext::DoNotOptimize(handle.index);
        vr::bench::BenchmarkContext::DoNotOptimize(handle.generation);
    }

    const std::uint32_t cache_hit_delta = host.Stats().cache_hit_count - initial_cache_hits;
    if (cache_hit_delta != iterations) {
        throw std::runtime_error("SkyEnvironmentGpuHost static cache-hit benchmark observed cache miss drift.");
    }

    bench_context_.AddItems(iterations);
    vr::bench::BenchmarkContext::DoNotOptimize(host.Stats().environment_count);
    vr::bench::BenchmarkContext::DoNotOptimize(host.Stats().cache_hit_count);
    vr::bench::BenchmarkContext::ClobberMemory();

    host.Shutdown(context);
}

VR_BENCHMARK_CASE(SceneRecorder3D_prepare_static_environment_cpu,
                  "core;scene;background;recorder;cpu") {
    vr::VulkanContext context{};
    vr::render::RenderTargetHost render_target{};
    vr::asset::TextureHost texture_host{};
    vr::render::DescriptorHost descriptor_host{};
    vr::render::PipelineHost pipeline_host{};
    vr::resource::SamplerHost sampler_host{};
    vr::render::UploadHost upload_host{};
    vr::render::SkyEnvironmentGpuHost sky_environment_host{};
    sky_environment_host.Initialize(context, texture_host, descriptor_host, sampler_host, {});

    vr::scene::SkyEnvironmentRenderState environment{};
    environment.mode = vr::scene::SkyEnvironmentMode::cubemap;
    environment.sky_texture_id = 61U;
    environment.irradiance_texture_id = 201U;
    environment.prefiltered_texture_id = 202U;
    environment.brdf_lut_texture_id = 203U;
    environment.tint = vr::ecs::Float4{.x = 1.0F, .y = 0.98F, .z = 0.95F, .w = 1.0F};
    environment.exposure = 1.0F;
    environment.sky_intensity = 1.0F;
    environment.diffuse_ibl_intensity = 1.0F;
    environment.specular_ibl_intensity = 1.0F;
    environment.rotation_y = 0.15F;
    environment.max_specular_lod = 5.0F;
    environment.revision = 37U;

    const vr::render::SkyEnvironmentGpuPrepareView sky_prepare_view{
        .device = context,
        .texture = texture_host,
        .upload = upload_host,
        .descriptor = descriptor_host,
        .sampler = sampler_host,
    };
    const vr::scene::SkyEnvironmentGpuHandle environment_gpu =
        sky_environment_host.RegisterOrUpdate(environment, sky_prepare_view);
    if (!environment_gpu.IsValid()) {
        throw std::runtime_error("SceneRecorder3D benchmark could not pre-register environment GPU handle.");
    }

    vr::render::RenderView3D view{};
    view.viewport.width = 1280.0F;
    view.viewport.height = 720.0F;
    view.scissor.width = 1280U;
    view.scissor.height = 720U;
    vr::render::RefreshRenderViewSignature(view);

    vr::render::RenderScenePacket3D packet =
        vr::render::MakeSingleViewScenePacket(view, 41U, vr::render::RenderScenePacketKind::world);
    packet.extra.environment = environment;
    packet.extra.environment_gpu = environment_gpu;
    packet.extra.ibl_environment_id = 99U;
    vr::render::RefreshRenderScenePacketSignature(packet);

    vr::render::SceneRecorder3D recorder{};
    recorder.Initialize();

    const vr::render::SceneRecorder3DPrepareView prepare_view{
        .device = context,
        .texture = &texture_host,
        .upload = &upload_host,
        .descriptor = &descriptor_host,
        .sky_environment = &sky_environment_host,
        .pipeline = &pipeline_host,
        .render_target = render_target,
        .sampler = &sampler_host,
    };

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t iteration = 0U; iteration < iterations; ++iteration) {
        recorder.PrepareFrame(prepare_view, packet);
        vr::bench::BenchmarkContext::DoNotOptimize(recorder.Stats().prepare_count);
        vr::bench::BenchmarkContext::DoNotOptimize(sky_environment_host.Stats().prepared_frame_count);
    }

    if (recorder.Stats().environment_gpu_resolve_count != 0U) {
        throw std::runtime_error("SceneRecorder3D static environment benchmark unexpectedly resolved GPU handle.");
    }

    bench_context_.AddItems(iterations);
    vr::bench::BenchmarkContext::DoNotOptimize(recorder.Stats().environment_prepare_count);
    vr::bench::BenchmarkContext::DoNotOptimize(recorder.FramePacket());
    vr::bench::BenchmarkContext::ClobberMemory();

    recorder.Shutdown(context);
    sky_environment_host.Shutdown(context);
}

} // namespace

