#include "Center/Memory/Container/Vector/McVector.hpp"
#include "support/bench_framework.hpp"
#include "vr/ecs/system/geometry_mesh_system.hpp"
#include "vr/ecs/system/geometry_runtime_system.hpp"
#include "vr/ecs/system/geometry_system.hpp"
#include "vr/ecs/system/transform_system.hpp"

#include <cstdint>

namespace {

using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
using GeometryRuntimeSystem3D = vr::ecs::GeometryRuntimeSystem<vr::ecs::Dim3>;
using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
using GeometryMeshSystem = vr::ecs::GeometryMeshSystem;
using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
template<typename T>
using GeometryBenchMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

constexpr std::uint32_t k_component_count = 1024U;
static_assert((k_component_count & (k_component_count - 1U)) == 0U);

void InitializeScene(GeometryBenchMcVector<Geometry3D>& components_,
                     GeometryBenchMcVector<Transform3D>& transforms_) {
    components_.resize(k_component_count);
    transforms_.resize(k_component_count);

    for (std::uint32_t i = 0U; i < k_component_count; ++i) {
        GeometryMeshSystem::Initialize(components_[i]);
        GeometryMeshSystem::SetMeshRoute(components_[i],
                                         1U + (i % 64U),
                                         i % 3U,
                                         static_cast<std::uint16_t>(i % 2U));
        GeometrySystem3D::SetMaterialId(components_[i], 1U + (i % 256U));
        GeometrySystem3D::SetDepthBin(components_[i], static_cast<std::uint16_t>(i % 1024U));
        GeometryMeshSystem::SetTopology(components_[i], vr::ecs::Geometry3DTopology::triangles);
        GeometryMeshSystem::SetShadingModel(components_[i], vr::ecs::Geometry3DShadingModel::lit);
        GeometryMeshSystem::SetAlbedoColor(components_[i],
                                           vr::ecs::Rgba8{
                                               static_cast<std::uint8_t>(40U + (i % 180U)),
                                               static_cast<std::uint8_t>(70U + ((i * 3U) % 160U)),
                                               static_cast<std::uint8_t>(90U + ((i * 5U) % 140U)),
                                               255U});
        GeometryMeshSystem::SetDepthTest(components_[i], true);
        GeometryMeshSystem::SetDepthWrite(components_[i], (i & 1U) == 0U);
        GeometryMeshSystem::SetDoubleSided(components_[i], (i & 3U) == 0U);
        GeometryMeshSystem::SetMaterialScalars(components_[i],
                                               static_cast<float>(i % 100U) * 0.01F,
                                               0.35F + static_cast<float>(i % 50U) * 0.01F,
                                               1.0F);
        GeometryMeshSystem::SetBounds(components_[i],
                                      vr::ecs::Float3{.x = -0.5F, .y = -0.5F, .z = -0.05F},
                                      vr::ecs::Float3{.x = 0.5F, .y = 0.5F, .z = 0.05F});

        TransformSystem3D::Initialize(transforms_[i]);
        TransformSystem3D::SetLocalPosition(transforms_[i],
                                            vr::ecs::Float3{
                                                .x = static_cast<float>(i % 64U) * 0.22F - 7.0F,
                                                .y = static_cast<float>((i / 64U) % 32U) * 0.18F - 3.0F,
                                                .z = static_cast<float>(i % 7U) * 0.12F - 0.4F});
        TransformSystem3D::SetLocalRotationEulerXyz(transforms_[i],
                                                    0.0F,
                                                    0.0F,
                                                    static_cast<float>(i % 360U) * 0.01745329251994329577F);
        TransformSystem3D::SetLocalScale(transforms_[i],
                                         vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F});
    }

    TransformSystem3D::UpdateHierarchy(transforms_.data(), k_component_count);
}

VR_BENCHMARK_CASE(EcsGeometryRuntimeSystem_dim3_build_1k_full_rebuild, "core;ecs;geometry;runtime;cpu") {
    GeometryBenchMcVector<Geometry3D> components{};
    GeometryBenchMcVector<Transform3D> transforms{};
    InitializeScene(components, transforms);

    vr::ecs::Geometry3DRuntimeScratch scratch{};
    GeometryRuntimeSystem3D::Reserve(scratch, k_component_count, k_component_count * 2U);

    vr::ecs::Geometry3DRuntimeBuildConfig build_config{};
    build_config.build_ordered_indices = true;

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const std::uint32_t hot_index = static_cast<std::uint32_t>(i) & (k_component_count - 1U);
        GeometrySystem3D::SetMaterialId(components[hot_index], 1U + ((hot_index + static_cast<std::uint32_t>(i)) & 255U));
        GeometrySystem3D::SetDepthBin(components[hot_index], static_cast<std::uint16_t>((hot_index + i) & 1023U));

        TransformSystem3D::SetLocalPosition(transforms[hot_index],
                                            vr::ecs::Float3{
                                                .x = static_cast<float>(hot_index % 64U) * 0.22F - 7.0F,
                                                .y = static_cast<float>((hot_index / 64U) % 32U) * 0.18F - 3.0F,
                                                .z = static_cast<float>((hot_index + i) % 11U) * 0.10F - 0.5F});
        TransformSystem3D::UpdateHierarchy(transforms.data(), k_component_count);

        const vr::ecs::Geometry3DRuntimeBuildStats stats =
            GeometryRuntimeSystem3D::Build(components.data(),
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
                            sizeof(vr::ecs::Geometry3DGpuInstance));
    vr::bench::BenchmarkContext::DoNotOptimize(scratch.draw_batches.size());
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(EcsGeometryRuntimeSystem_dim3_build_1k_transform_only, "core;ecs;geometry;runtime;cpu") {
    GeometryBenchMcVector<Geometry3D> components{};
    GeometryBenchMcVector<Transform3D> transforms{};
    InitializeScene(components, transforms);

    vr::ecs::Geometry3DRuntimeScratch scratch{};
    GeometryRuntimeSystem3D::Reserve(scratch, k_component_count, k_component_count * 2U);

    vr::ecs::Geometry3DRuntimeBuildConfig build_config{};
    build_config.build_ordered_indices = true;

    const vr::ecs::Geometry3DRuntimeBuildStats warmup_stats =
        GeometryRuntimeSystem3D::Build(components.data(),
                                       transforms.data(),
                                       k_component_count,
                                       scratch,
                                       build_config);
    vr::bench::BenchmarkContext::DoNotOptimize(warmup_stats.emitted_batch_count);

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const std::uint32_t hot_index = static_cast<std::uint32_t>(i) & (k_component_count - 1U);
        TransformSystem3D::SetLocalPosition(transforms[hot_index],
                                            vr::ecs::Float3{
                                                .x = static_cast<float>(hot_index % 64U) * 0.22F - 7.0F,
                                                .y = static_cast<float>((hot_index / 64U) % 32U) * 0.18F - 3.0F,
                                                .z = static_cast<float>((hot_index + i) % 17U) * 0.09F - 0.6F});
        TransformSystem3D::SetLocalRotationEulerXyz(transforms[hot_index],
                                                    0.0F,
                                                    0.0F,
                                                    static_cast<float>((hot_index + i) % 720U) * 0.00872664625997164788F);
        TransformSystem3D::UpdateHierarchy(transforms.data(), k_component_count);

        const vr::ecs::Geometry3DRuntimeBuildStats stats =
            GeometryRuntimeSystem3D::Build(components.data(),
                                           transforms.data(),
                                           k_component_count,
                                           scratch,
                                           build_config);
        vr::bench::BenchmarkContext::DoNotOptimize(stats.transform_only_update);
        vr::bench::BenchmarkContext::DoNotOptimize(stats.cache_reused);
    }

    bench_context_.AddItems(iterations * k_component_count);
    bench_context_.AddBytes(iterations *
                            static_cast<std::uint64_t>(scratch.instances.size()) *
                            sizeof(vr::ecs::Geometry3DGpuInstance));
    vr::bench::BenchmarkContext::DoNotOptimize(scratch.draw_batches.size());
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(EcsGeometryRuntimeSystem_dim3_build_1k_transform_only_dirty_hint, "core;ecs;geometry;runtime;cpu") {
    GeometryBenchMcVector<Geometry3D> components{};
    GeometryBenchMcVector<Transform3D> transforms{};
    InitializeScene(components, transforms);

    vr::ecs::Geometry3DRuntimeScratch scratch{};
    GeometryRuntimeSystem3D::Reserve(scratch, k_component_count, k_component_count * 2U);

    vr::ecs::Geometry3DRuntimeBuildConfig build_config{};
    build_config.build_ordered_indices = true;

    const vr::ecs::Geometry3DRuntimeBuildStats warmup_stats =
        GeometryRuntimeSystem3D::Build(components.data(),
                                       transforms.data(),
                                       k_component_count,
                                       scratch,
                                       build_config);
    vr::bench::BenchmarkContext::DoNotOptimize(warmup_stats.emitted_batch_count);

    std::uint64_t geometry_signature = warmup_stats.geometry_signature;
    std::uint64_t transform_signature = warmup_stats.transform_signature;

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const std::uint32_t hot_index = static_cast<std::uint32_t>(i) & (k_component_count - 1U);
        TransformSystem3D::SetLocalPosition(transforms[hot_index],
                                            vr::ecs::Float3{
                                                .x = static_cast<float>(hot_index % 64U) * 0.22F - 7.0F,
                                                .y = static_cast<float>((hot_index / 64U) % 32U) * 0.18F - 3.0F,
                                                .z = static_cast<float>((hot_index + i) % 19U) * 0.07F - 0.65F});
        TransformSystem3D::SetLocalRotationEulerXyz(transforms[hot_index],
                                                    0.0F,
                                                    0.0F,
                                                    static_cast<float>((hot_index + i) % 720U) * 0.00872664625997164788F);
        TransformSystem3D::UpdateHierarchy(transforms.data(), k_component_count);

        ++transform_signature;

        vr::ecs::Geometry3DRuntimeBuildHint build_hint{};
        build_hint.external_geometry_signature = geometry_signature;
        build_hint.external_transform_signature = transform_signature;
        build_hint.transform_dirty_component_indices = &hot_index;
        build_hint.transform_dirty_component_count = 1U;
        build_hint.use_external_geometry_signature = 1U;
        build_hint.use_external_transform_signature = 1U;

        const vr::ecs::Geometry3DRuntimeBuildStats stats =
            GeometryRuntimeSystem3D::Build(components.data(),
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
                            sizeof(vr::ecs::Geometry3DGpuInstance));
    vr::bench::BenchmarkContext::DoNotOptimize(scratch.draw_batches.size());
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(EcsGeometryRuntimeSystem_dim3_build_1k_candidate_visibility_half, "core;ecs;geometry;runtime;cpu") {
    GeometryBenchMcVector<Geometry3D> components{};
    GeometryBenchMcVector<Transform3D> transforms{};
    GeometryBenchMcVector<std::uint32_t> visible_indices{};
    InitializeScene(components, transforms);

    visible_indices.reserve(k_component_count / 2U);
    for (std::uint32_t i = 0U; i < k_component_count; ++i) {
        if ((i & 1U) == 0U) {
            visible_indices.push_back(i);
        }
    }

    vr::ecs::Geometry3DRuntimeScratch scratch{};
    GeometryRuntimeSystem3D::Reserve(scratch, k_component_count, k_component_count);

    vr::ecs::Geometry3DRuntimeBuildConfig build_config{};
    build_config.build_ordered_indices = true;

    vr::ecs::Geometry3DRuntimeBuildHint build_hint{};
    build_hint.visible_component_indices = visible_indices.data();
    build_hint.visible_component_count = static_cast<std::uint32_t>(visible_indices.size());
    build_hint.use_visible_component_indices = 1U;

    const vr::ecs::Geometry3DRuntimeBuildStats warmup_stats =
        GeometryRuntimeSystem3D::Build(components.data(),
                                       transforms.data(),
                                       k_component_count,
                                       scratch,
                                       build_config,
                                       build_hint);
    vr::bench::BenchmarkContext::DoNotOptimize(warmup_stats.emitted_instance_count);

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        // Update one hidden (odd) component to simulate off-screen transform churn.
        const std::uint32_t hidden_hot_index =
            (static_cast<std::uint32_t>(i) & ((k_component_count / 2U) - 1U)) * 2U + 1U;
        TransformSystem3D::SetLocalPosition(transforms[hidden_hot_index],
                                            vr::ecs::Float3{
                                                .x = static_cast<float>(hidden_hot_index % 64U) * 0.22F - 7.0F,
                                                .y = static_cast<float>((hidden_hot_index / 64U) % 32U) * 0.18F - 3.0F,
                                                .z = static_cast<float>((hidden_hot_index + i) % 23U) * 0.06F - 0.8F});
        TransformSystem3D::UpdateHierarchy(transforms.data(), k_component_count);

        const vr::ecs::Geometry3DRuntimeBuildStats stats =
            GeometryRuntimeSystem3D::Build(components.data(),
                                           transforms.data(),
                                           k_component_count,
                                           scratch,
                                           build_config,
                                           build_hint);
        vr::bench::BenchmarkContext::DoNotOptimize(stats.cache_status);
        vr::bench::BenchmarkContext::DoNotOptimize(stats.cache_reused);
        vr::bench::BenchmarkContext::DoNotOptimize(stats.used_visible_component_indices);
    }

    bench_context_.AddItems(iterations * static_cast<std::uint64_t>(visible_indices.size()));
    bench_context_.AddBytes(iterations *
                            static_cast<std::uint64_t>(scratch.instances.size()) *
                            sizeof(vr::ecs::Geometry3DGpuInstance));
    vr::bench::BenchmarkContext::DoNotOptimize(scratch.draw_batches.size());
    vr::bench::BenchmarkContext::ClobberMemory();
}

} // namespace
