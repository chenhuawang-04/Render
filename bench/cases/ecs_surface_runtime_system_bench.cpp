#include "Center/Memory/Container/Vector/McVector.hpp"
#include "support/bench_framework.hpp"
#include "vr/ecs/system/surface_runtime_system.hpp"
#include "vr/ecs/system/surface_system.hpp"
#include "vr/ecs/system/transform_system.hpp"

#include <cstdint>

namespace {

using Surface3D = vr::ecs::Surface<vr::ecs::Dim3>;
using SurfaceSystem3D = vr::ecs::SurfaceSystem<vr::ecs::Dim3>;
using SurfaceRuntimeSystem3D = vr::ecs::SurfaceRuntimeSystem<vr::ecs::Dim3>;
using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
template<typename T>
using SurfaceBenchMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

constexpr std::uint32_t k_component_count = 1024U;
static_assert((k_component_count & (k_component_count - 1U)) == 0U);

void InitializeScene(SurfaceBenchMcVector<Surface3D>& components_,
                     SurfaceBenchMcVector<Transform3D>& transforms_) {
    components_.resize(k_component_count);
    transforms_.resize(k_component_count);

    for (std::uint32_t i = 0U; i < k_component_count; ++i) {
        SurfaceSystem3D::Initialize(components_[i]);
        SurfaceSystem3D::SetTextureRoute(components_[i],
                                         1U + (i % 128U),
                                         1U + (i % 16U),
                                         static_cast<std::uint16_t>(i % 2U),
                                         static_cast<std::uint16_t>(i % 8U));
        SurfaceSystem3D::SetMaterialId(components_[i], 1U + (i % 64U));
        SurfaceSystem3D::SetDepthBin(components_[i], static_cast<std::uint16_t>(i % 1024U));
        SurfaceSystem3D::SetOpacity(components_[i], 0.35F + static_cast<float>(i % 65U) * 0.01F);
        SurfaceSystem3D::SetDepthTest(components_[i], true);
        SurfaceSystem3D::SetDepthWrite(components_[i], (i & 1U) == 0U);
        SurfaceSystem3D::SetDoubleSided(components_[i], (i & 3U) == 0U);
        SurfaceSystem3D::SetUvTransform(components_[i],
                                        1.0F + static_cast<float>(i % 4U) * 0.1F,
                                        1.0F + static_cast<float>(i % 6U) * 0.08F,
                                        static_cast<float>(i % 11U) * 0.01F,
                                        static_cast<float>(i % 7U) * 0.01F);

        TransformSystem3D::Initialize(transforms_[i]);
        TransformSystem3D::SetLocalPosition(transforms_[i],
                                            vr::ecs::Float3{
                                                .x = static_cast<float>(i % 64U) * 0.22F - 7.0F,
                                                .y = static_cast<float>((i / 64U) % 32U) * 0.18F - 3.0F,
                                                .z = static_cast<float>(i % 11U) * 0.11F - 0.7F});
        TransformSystem3D::SetLocalRotationEulerXyz(transforms_[i],
                                                    0.0F,
                                                    0.0F,
                                                    static_cast<float>(i % 360U) * 0.01745329251994329577F);
        TransformSystem3D::SetLocalScale(transforms_[i],
                                         vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F});
    }

    TransformSystem3D::UpdateHierarchy(transforms_.data(), k_component_count);
}

VR_BENCHMARK_CASE(EcsSurfaceRuntimeSystem_dim3_build_1k_full_rebuild, "core;ecs;surface;runtime;cpu") {
    SurfaceBenchMcVector<Surface3D> components{};
    SurfaceBenchMcVector<Transform3D> transforms{};
    InitializeScene(components, transforms);

    vr::ecs::Surface3DRuntimeScratch scratch{};
    SurfaceRuntimeSystem3D::Reserve(scratch, k_component_count, k_component_count * 2U);

    vr::ecs::Surface3DRuntimeBuildConfig build_config{};
    build_config.build_ordered_indices = true;

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const std::uint32_t hot_index = static_cast<std::uint32_t>(i) & (k_component_count - 1U);
        SurfaceSystem3D::SetMaterialId(components[hot_index], 1U + ((hot_index + static_cast<std::uint32_t>(i)) & 63U));
        SurfaceSystem3D::SetDepthBin(components[hot_index], static_cast<std::uint16_t>((hot_index + i) & 1023U));

        TransformSystem3D::SetLocalPosition(transforms[hot_index],
                                            vr::ecs::Float3{
                                                .x = static_cast<float>(hot_index % 64U) * 0.22F - 7.0F,
                                                .y = static_cast<float>((hot_index / 64U) % 32U) * 0.18F - 3.0F,
                                                .z = static_cast<float>((hot_index + i) % 13U) * 0.09F - 0.65F});
        TransformSystem3D::UpdateHierarchy(transforms.data(), k_component_count);

        const vr::ecs::Surface3DRuntimeBuildStats stats =
            SurfaceRuntimeSystem3D::Build(components.data(),
                                          transforms.data(),
                                          k_component_count,
                                          scratch,
                                          build_config);
        vr::bench::BenchmarkContext::DoNotOptimize(stats.emitted_instance_count);
        vr::bench::BenchmarkContext::DoNotOptimize(stats.emitted_batch_count);
    }

    bench_context_.AddItems(iterations * k_component_count);
    bench_context_.AddBytes(iterations *
                            static_cast<std::uint64_t>(scratch.instances.size()) *
                            sizeof(vr::ecs::Surface3DGpuInstance));
    vr::bench::BenchmarkContext::DoNotOptimize(scratch.draw_batches.size());
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(EcsSurfaceRuntimeSystem_dim3_build_1k_transform_only_dirty_hint, "core;ecs;surface;runtime;cpu") {
    SurfaceBenchMcVector<Surface3D> components{};
    SurfaceBenchMcVector<Transform3D> transforms{};
    InitializeScene(components, transforms);

    vr::ecs::Surface3DRuntimeScratch scratch{};
    SurfaceRuntimeSystem3D::Reserve(scratch, k_component_count, k_component_count * 2U);

    vr::ecs::Surface3DRuntimeBuildConfig build_config{};
    build_config.build_ordered_indices = true;

    const vr::ecs::Surface3DRuntimeBuildStats warmup_stats =
        SurfaceRuntimeSystem3D::Build(components.data(),
                                      transforms.data(),
                                      k_component_count,
                                      scratch,
                                      build_config);
    vr::bench::BenchmarkContext::DoNotOptimize(warmup_stats.emitted_batch_count);

    std::uint64_t surface_signature = warmup_stats.surface_signature;
    std::uint64_t transform_signature = warmup_stats.transform_signature;

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const std::uint32_t hot_index = static_cast<std::uint32_t>(i) & (k_component_count - 1U);
        TransformSystem3D::SetLocalPosition(transforms[hot_index],
                                            vr::ecs::Float3{
                                                .x = static_cast<float>(hot_index % 64U) * 0.22F - 7.0F,
                                                .y = static_cast<float>((hot_index / 64U) % 32U) * 0.18F - 3.0F,
                                                .z = static_cast<float>((hot_index + i) % 19U) * 0.07F - 0.60F});
        TransformSystem3D::SetLocalRotationEulerXyz(transforms[hot_index],
                                                    0.0F,
                                                    0.0F,
                                                    static_cast<float>((hot_index + i) % 720U) * 0.00872664625997164788F);
        TransformSystem3D::UpdateHierarchy(transforms.data(), k_component_count);
        ++transform_signature;

        vr::ecs::Surface3DRuntimeBuildHint build_hint{};
        build_hint.external_surface_signature = surface_signature;
        build_hint.external_transform_signature = transform_signature;
        build_hint.transform_dirty_component_indices = &hot_index;
        build_hint.transform_dirty_component_count = 1U;
        build_hint.use_external_surface_signature = 1U;
        build_hint.use_external_transform_signature = 1U;

        const vr::ecs::Surface3DRuntimeBuildStats stats =
            SurfaceRuntimeSystem3D::Build(components.data(),
                                          transforms.data(),
                                          k_component_count,
                                          scratch,
                                          build_config,
                                          build_hint);
        vr::bench::BenchmarkContext::DoNotOptimize(stats.transform_only_update);
        vr::bench::BenchmarkContext::DoNotOptimize(stats.cache_reused);
        vr::bench::BenchmarkContext::DoNotOptimize(stats.transform_update_from_dirty_hint);
    }

    bench_context_.AddItems(iterations * k_component_count);
    bench_context_.AddBytes(iterations *
                            static_cast<std::uint64_t>(scratch.instances.size()) *
                            sizeof(vr::ecs::Surface3DGpuInstance));
    vr::bench::BenchmarkContext::DoNotOptimize(scratch.draw_batches.size());
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(EcsSurfaceRuntimeSystem_dim3_build_1k_candidate_visibility_half, "core;ecs;surface;runtime;cpu;candidates") {
    SurfaceBenchMcVector<Surface3D> components{};
    SurfaceBenchMcVector<Transform3D> transforms{};
    InitializeScene(components, transforms);

    vr::ecs::Surface3DRuntimeScratch scratch{};
    SurfaceRuntimeSystem3D::Reserve(scratch, k_component_count, k_component_count * 2U);

    SurfaceBenchMcVector<std::uint32_t> visible_candidates{};
    visible_candidates.reserve(k_component_count / 2U);
    for (std::uint32_t i = 0U; i < k_component_count; i += 2U) {
        visible_candidates.push_back(i);
    }
    const std::uint32_t candidate_count = static_cast<std::uint32_t>(visible_candidates.size());

    vr::ecs::Surface3DRuntimeBuildConfig build_config{};
    build_config.build_ordered_indices = true;

    vr::ecs::Surface3DRuntimeBuildHint build_hint{};
    build_hint.visible_component_indices = visible_candidates.data();
    build_hint.visible_component_count = candidate_count;
    build_hint.use_visible_component_indices = 1U;

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const std::uint32_t hot_slot = static_cast<std::uint32_t>(i) & (candidate_count - 1U);
        const std::uint32_t hot_index = visible_candidates[hot_slot];

        SurfaceSystem3D::SetMaterialId(components[hot_index],
                                       1U + ((hot_index + static_cast<std::uint32_t>(i)) & 63U));
        SurfaceSystem3D::SetDepthBin(components[hot_index],
                                     static_cast<std::uint16_t>((hot_index + i) & 1023U));

        TransformSystem3D::SetLocalPosition(transforms[hot_index],
                                            vr::ecs::Float3{
                                                .x = static_cast<float>(hot_index % 64U) * 0.22F - 7.0F,
                                                .y = static_cast<float>((hot_index / 64U) % 32U) * 0.18F - 3.0F,
                                                .z = static_cast<float>((hot_index + i) % 17U) * 0.08F - 0.62F});
        TransformSystem3D::UpdateHierarchy(transforms.data(), k_component_count);

        const vr::ecs::Surface3DRuntimeBuildStats stats =
            SurfaceRuntimeSystem3D::Build(components.data(),
                                          transforms.data(),
                                          k_component_count,
                                          scratch,
                                          build_config,
                                          build_hint);
        vr::bench::BenchmarkContext::DoNotOptimize(stats.batch.visible_count);
        vr::bench::BenchmarkContext::DoNotOptimize(stats.candidate_component_count);
        vr::bench::BenchmarkContext::DoNotOptimize(stats.used_visible_component_indices);
    }

    bench_context_.AddItems(iterations * candidate_count);
    bench_context_.AddBytes(iterations *
                            static_cast<std::uint64_t>(scratch.instances.size()) *
                            sizeof(vr::ecs::Surface3DGpuInstance));
    vr::bench::BenchmarkContext::DoNotOptimize(scratch.draw_batches.size());
    vr::bench::BenchmarkContext::ClobberMemory();
}

} // namespace
