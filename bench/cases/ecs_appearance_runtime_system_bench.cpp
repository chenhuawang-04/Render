#include "support/bench_framework.hpp"
#include "vr/ecs/system/appearance_link_system.hpp"
#include "vr/ecs/system/appearance_runtime_system.hpp"
#include "vr/render/appearance_frame_coordinator.hpp"

#include <cstdint>

namespace {

template<typename T>
using AppearanceBenchMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

VR_BENCHMARK_CASE(EcsAppearanceRuntimeSystem_dim2_build_1k_full_rebuild,
                  "core;ecs;appearance;runtime;cpu") {
    using Appearance2D = vr::ecs::Appearance<vr::ecs::Dim2>;
    using AppearanceSystem2D = vr::ecs::AppearanceSystem<vr::ecs::Dim2>;
    using RuntimeSystem2D = vr::ecs::AppearanceRuntimeSystem<vr::ecs::Dim2>;

    constexpr std::uint32_t component_count = 1024U;
    AppearanceBenchMcVector<Appearance2D> components{};
    components.resize(component_count);

    for (std::uint32_t i = 0U; i < component_count; ++i) {
        AppearanceSystem2D::Initialize(components[i]);
        AppearanceSystem2D::SetLayer(components[i], static_cast<std::int16_t>((i % 256U) - 128));
        AppearanceSystem2D::SetPatternSurface(components[i], 1U + (i % 64U));
        AppearanceSystem2D::SetMaskSurface(components[i], 11U + (i % 16U));
        AppearanceSystem2D::SetSurfaceSamplerId(components[i], 3U);
    }

    vr::ecs::AppearanceRuntimeScratch<vr::ecs::Dim2> scratch{};

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        vr::ecs::AppearanceRuntimeBuildConfig config{};
        config.force_full_rebuild = true;
        config.rebuild_keys_even_if_clean = false;
        config.merge_gap = 0U;
        const vr::ecs::AppearanceRuntimeBuildStats stats = RuntimeSystem2D::Build(components.data(),
                                                                                   component_count,
                                                                                   scratch,
                                                                                   RuntimeSystem2D::DefaultPipelinePolicy(),
                                                                                   RuntimeSystem2D::DefaultSortPolicy(),
                                                                                   config);
        vr::bench::BenchmarkContext::DoNotOptimize(stats.updated_record_count);
    }

    bench_context_.AddItems(iterations * component_count);
    bench_context_.AddBytes(iterations * component_count * sizeof(vr::ecs::AppearanceGpuRecord<vr::ecs::Dim2>));
    vr::bench::BenchmarkContext::DoNotOptimize(RuntimeSystem2D::GpuRecordCount(scratch));
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(EcsAppearanceRuntimeSystem_dim3_build_1k_dirty_hint,
                  "core;ecs;appearance;runtime;cpu") {
    using Appearance3D = vr::ecs::Appearance<vr::ecs::Dim3>;
    using AppearanceSystem3D = vr::ecs::AppearanceSystem<vr::ecs::Dim3>;
    using RuntimeSystem3D = vr::ecs::AppearanceRuntimeSystem<vr::ecs::Dim3>;

    constexpr std::uint32_t component_count = 1024U;
    AppearanceBenchMcVector<Appearance3D> components{};
    components.resize(component_count);

    for (std::uint32_t i = 0U; i < component_count; ++i) {
        AppearanceSystem3D::Initialize(components[i]);
        AppearanceSystem3D::SetBaseColorSurface(components[i], vr::render::MakeAppearanceSampledSurfaceHandle(100U + (i % 128U)));
        AppearanceSystem3D::SetNormalSurface(components[i], vr::render::MakeAppearanceSampledSurfaceHandle(200U + (i % 128U)));
        AppearanceSystem3D::SetMetalRoughSurface(components[i], vr::render::MakeAppearanceSampledSurfaceHandle(300U + (i % 128U)));
        AppearanceSystem3D::SetOcclusionSurface(components[i], vr::render::MakeAppearanceSampledSurfaceHandle(400U + (i % 128U)));
        AppearanceSystem3D::SetEmissiveSurface(components[i], vr::render::MakeAppearanceSampledSurfaceHandle(500U + (i % 128U)));
        AppearanceSystem3D::SetSurfaceSamplerId(components[i], 4U);
    }

    vr::ecs::AppearanceRuntimeScratch<vr::ecs::Dim3> scratch{};
    (void)RuntimeSystem3D::Build(components.data(), component_count, scratch);

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const std::uint32_t hot_index = static_cast<std::uint32_t>(i & (component_count - 1U));
        AppearanceSystem3D::SetRoughness(components[hot_index],
                                         static_cast<float>((hot_index + static_cast<std::uint32_t>(i)) & 255U) / 255.0F);

        vr::ecs::AppearanceRuntimeBuildHint hint{};
        hint.use_dirty_component_indices = 1U;
        hint.dirty_component_indices = &hot_index;
        hint.dirty_component_count = 1U;
        const vr::ecs::AppearanceRuntimeBuildStats stats = RuntimeSystem3D::Build(components.data(),
                                                                                   component_count,
                                                                                   scratch,
                                                                                   RuntimeSystem3D::DefaultPipelinePolicy(),
                                                                                   RuntimeSystem3D::DefaultSortPolicy(),
                                                                                   RuntimeSystem3D::DefaultBuildConfig(),
                                                                                   hint);
        vr::bench::BenchmarkContext::DoNotOptimize(stats.updated_record_count);
    }

    bench_context_.AddItems(iterations);
    bench_context_.AddBytes(iterations * sizeof(vr::ecs::AppearanceGpuRecord<vr::ecs::Dim3>));
    vr::bench::BenchmarkContext::DoNotOptimize(RuntimeSystem3D::UploadRangeCount(scratch));
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(EcsAppearanceLinkSystem_dim3_geometry_1k_dirty_hint,
                  "core;ecs;appearance;link;cpu") {
    using Appearance3D = vr::ecs::Appearance<vr::ecs::Dim3>;
    using AppearanceSystem3D = vr::ecs::AppearanceSystem<vr::ecs::Dim3>;
    using AppearanceRuntimeSystem3D = vr::ecs::AppearanceRuntimeSystem<vr::ecs::Dim3>;
    using LinkSystem3D = vr::ecs::AppearanceLinkSystem<vr::ecs::Dim3>;
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;

    constexpr std::uint32_t component_count = 1024U;
    AppearanceBenchMcVector<Appearance3D> appearance_components{};
    appearance_components.resize(component_count);
    for (std::uint32_t i = 0U; i < component_count; ++i) {
        AppearanceSystem3D::Initialize(appearance_components[i]);
        AppearanceSystem3D::SetBaseColorSurface(appearance_components[i], vr::render::MakeAppearanceSampledSurfaceHandle(100U + (i % 128U)));
        AppearanceSystem3D::SetNormalSurface(appearance_components[i], vr::render::MakeAppearanceSampledSurfaceHandle(200U + (i % 128U)));
        AppearanceSystem3D::SetSurfaceSamplerId(appearance_components[i], 2U);
    }

    vr::ecs::AppearanceRuntimeScratch<vr::ecs::Dim3> appearance_scratch{};
    (void)AppearanceRuntimeSystem3D::Build(appearance_components.data(),
                                           component_count,
                                           appearance_scratch);

    AppearanceBenchMcVector<Geometry3D> geometry_components{};
    geometry_components.resize(component_count);
    for (std::uint32_t i = 0U; i < component_count; ++i) {
        GeometrySystem3D::Initialize(geometry_components[i]);
        GeometrySystem3D::SetGeometryId(geometry_components[i], 1U + i);
        GeometrySystem3D::SetAppearanceHandle(geometry_components[i],
                                              appearance_components[i].runtime.gpu_record_handle);
    }

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const std::uint32_t hot_index = static_cast<std::uint32_t>(i & (component_count - 1U));
        AppearanceSystem3D::SetRoughness(appearance_components[hot_index],
                                         static_cast<float>((hot_index + static_cast<std::uint32_t>(i)) & 255U) / 255.0F);
        vr::ecs::AppearanceRuntimeBuildHint runtime_hint{};
        runtime_hint.use_dirty_component_indices = 1U;
        runtime_hint.dirty_component_indices = &hot_index;
        runtime_hint.dirty_component_count = 1U;
        (void)AppearanceRuntimeSystem3D::Build(appearance_components.data(),
                                               component_count,
                                               appearance_scratch,
                                               AppearanceRuntimeSystem3D::DefaultPipelinePolicy(),
                                               AppearanceRuntimeSystem3D::DefaultSortPolicy(),
                                               AppearanceRuntimeSystem3D::DefaultBuildConfig(),
                                               runtime_hint);

        const vr::ecs::AppearanceLinkStats link_stats = LinkSystem3D::ApplyToGeometryAligned(
            geometry_components.data(),
            component_count,
            appearance_components.data(),
            component_count,
            &hot_index,
            1U);
        vr::bench::BenchmarkContext::DoNotOptimize(link_stats.updated_count);
    }

    bench_context_.AddItems(iterations);
    bench_context_.AddBytes(iterations * sizeof(vr::ecs::GeometryRuntimeRoute));
    vr::bench::BenchmarkContext::DoNotOptimize(geometry_components[0U].runtime.route.sort_key);
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(EcsAppearancePrepareStage_dim3_dual_renderer_duplicate_build_1k,
                  "core;ecs;appearance;prepare;cpu;dual_renderer") {
    using Appearance3D = vr::ecs::Appearance<vr::ecs::Dim3>;
    using AppearanceSystem3D = vr::ecs::AppearanceSystem<vr::ecs::Dim3>;
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
    using Surface3D = vr::ecs::Surface<vr::ecs::Dim3>;
    using SurfaceSystem3D = vr::ecs::SurfaceSystem<vr::ecs::Dim3>;
    using PrepareStage3D = vr::render::AppearancePrepareStage<vr::ecs::Dim3>;

    constexpr std::uint32_t component_count = 1024U;
    AppearanceBenchMcVector<Appearance3D> appearance_components{};
    appearance_components.resize(component_count);
    AppearanceBenchMcVector<Geometry3D> geometry_components{};
    geometry_components.resize(component_count);
    AppearanceBenchMcVector<Surface3D> surface_components{};
    surface_components.resize(component_count);

    for (std::uint32_t i = 0U; i < component_count; ++i) {
        AppearanceSystem3D::Initialize(appearance_components[i]);
        AppearanceSystem3D::SetBaseColorSurface(appearance_components[i], vr::render::MakeAppearanceSampledSurfaceHandle(100U + (i % 128U)));
        AppearanceSystem3D::SetNormalSurface(appearance_components[i], vr::render::MakeAppearanceSampledSurfaceHandle(200U + (i % 128U)));
        AppearanceSystem3D::SetMetalRoughSurface(appearance_components[i], vr::render::MakeAppearanceSampledSurfaceHandle(300U + (i % 64U)));
        AppearanceSystem3D::SetSurfaceSamplerId(appearance_components[i], 3U);

        GeometrySystem3D::Initialize(geometry_components[i]);
        GeometrySystem3D::SetGeometryId(geometry_components[i], 1U + i);

        SurfaceSystem3D::Initialize(surface_components[i]);
        SurfaceSystem3D::SetSource(surface_components[i], vr::ecs::SurfaceSampledSource3DDesc{.surface_id = 500U + i});
    }

    vr::ecs::AppearanceRuntimeScratch<vr::ecs::Dim3> scratch_geometry{};
    vr::ecs::AppearanceRuntimeScratch<vr::ecs::Dim3> scratch_surface{};

    (void)PrepareStage3D::BuildRuntimeOnly(appearance_components.data(),
                                           component_count,
                                           nullptr,
                                           0U,
                                           scratch_geometry);
    for (std::uint32_t i = 0U; i < component_count; ++i) {
        const auto handle = appearance_components[i].runtime.gpu_record_handle;
        GeometrySystem3D::SetAppearanceHandle(geometry_components[i], handle);
        SurfaceSystem3D::SetAppearanceHandle(surface_components[i], handle);
    }

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const std::uint32_t hot_index = static_cast<std::uint32_t>(i & (component_count - 1U));
        AppearanceSystem3D::SetRoughness(appearance_components[hot_index],
                                         static_cast<float>((hot_index + static_cast<std::uint32_t>(i)) & 255U) / 255.0F);

        (void)PrepareStage3D::BuildAndLinkGeometry(appearance_components.data(),
                                                   component_count,
                                                   &hot_index,
                                                   1U,
                                                   scratch_geometry,
                                                   geometry_components.data(),
                                                   component_count);
        (void)PrepareStage3D::BuildAndLinkSurface(appearance_components.data(),
                                                  component_count,
                                                  &hot_index,
                                                  1U,
                                                  scratch_surface,
                                                  surface_components.data(),
                                                  component_count);
    }

    bench_context_.AddItems(iterations * component_count * 2U);
    bench_context_.AddBytes(iterations * component_count *
                            (sizeof(vr::ecs::AppearanceGpuRecord<vr::ecs::Dim3>) * 2ULL));
    vr::bench::BenchmarkContext::DoNotOptimize(geometry_components[0U].runtime.route.sort_key);
    vr::bench::BenchmarkContext::DoNotOptimize(surface_components[0U].runtime.route.sort_key);
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(EcsAppearanceFrameCoordinator_dim3_dual_renderer_shared_build_1k,
                  "core;ecs;appearance;prepare;cpu;dual_renderer;coordinator") {
    using Appearance3D = vr::ecs::Appearance<vr::ecs::Dim3>;
    using AppearanceSystem3D = vr::ecs::AppearanceSystem<vr::ecs::Dim3>;
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
    using Surface3D = vr::ecs::Surface<vr::ecs::Dim3>;
    using SurfaceSystem3D = vr::ecs::SurfaceSystem<vr::ecs::Dim3>;
    using Coordinator3D = vr::render::AppearanceFrameCoordinator<vr::ecs::Dim3>;

    constexpr std::uint32_t component_count = 1024U;
    AppearanceBenchMcVector<Appearance3D> appearance_components{};
    appearance_components.resize(component_count);
    AppearanceBenchMcVector<Geometry3D> geometry_components{};
    geometry_components.resize(component_count);
    AppearanceBenchMcVector<Surface3D> surface_components{};
    surface_components.resize(component_count);

    for (std::uint32_t i = 0U; i < component_count; ++i) {
        AppearanceSystem3D::Initialize(appearance_components[i]);
        AppearanceSystem3D::SetBaseColorSurface(appearance_components[i], vr::render::MakeAppearanceSampledSurfaceHandle(100U + (i % 128U)));
        AppearanceSystem3D::SetNormalSurface(appearance_components[i], vr::render::MakeAppearanceSampledSurfaceHandle(200U + (i % 128U)));
        AppearanceSystem3D::SetMetalRoughSurface(appearance_components[i], vr::render::MakeAppearanceSampledSurfaceHandle(300U + (i % 64U)));
        AppearanceSystem3D::SetSurfaceSamplerId(appearance_components[i], 3U);

        GeometrySystem3D::Initialize(geometry_components[i]);
        GeometrySystem3D::SetGeometryId(geometry_components[i], 1U + i);

        SurfaceSystem3D::Initialize(surface_components[i]);
        SurfaceSystem3D::SetSource(surface_components[i], vr::ecs::SurfaceSampledSource3DDesc{.surface_id = 500U + i});
    }

    Coordinator3D coordinator{};
    coordinator.SetAppearanceData(appearance_components.data(), component_count);
    coordinator.Reserve(component_count);
    (void)coordinator.PrepareFrame(0U);
    for (std::uint32_t i = 0U; i < component_count; ++i) {
        const auto handle = appearance_components[i].runtime.gpu_record_handle;
        GeometrySystem3D::SetAppearanceHandle(geometry_components[i], handle);
        SurfaceSystem3D::SetAppearanceHandle(surface_components[i], handle);
    }

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const std::uint32_t hot_index = static_cast<std::uint32_t>(i & (component_count - 1U));
        AppearanceSystem3D::SetRoughness(appearance_components[hot_index],
                                         static_cast<float>((hot_index + static_cast<std::uint32_t>(i)) & 255U) / 255.0F);
        coordinator.SetAppearanceData(appearance_components.data(), component_count);
        coordinator.SetDirtyHint(&hot_index, 1U);

        const std::uint32_t frame_index = static_cast<std::uint32_t>(i + 1U);
        (void)coordinator.PrepareFrame(frame_index);
        (void)coordinator.LinkGeometry(geometry_components.data(), component_count, frame_index);
        (void)coordinator.PrepareFrame(frame_index); // reused in same frame
        (void)coordinator.LinkSurface(surface_components.data(), component_count, frame_index);
    }

    bench_context_.AddItems(iterations * component_count * 2U);
    bench_context_.AddBytes(iterations * component_count *
                            (sizeof(vr::ecs::AppearanceGpuRecord<vr::ecs::Dim3>) +
                             sizeof(vr::ecs::GeometryRuntimeRoute) +
                             sizeof(vr::ecs::SurfaceRuntimeRoute)));
    vr::bench::BenchmarkContext::DoNotOptimize(geometry_components[0U].runtime.route.sort_key);
    vr::bench::BenchmarkContext::DoNotOptimize(surface_components[0U].runtime.route.sort_key);
    vr::bench::BenchmarkContext::DoNotOptimize(coordinator.Stats().runtime_build_call_count);
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(EcsAppearanceFrameCoordinator_dim3_link_incremental_sparse_1k,
                  "core;ecs;appearance;link;cpu;coordinator;incremental") {
    using Appearance3D = vr::ecs::Appearance<vr::ecs::Dim3>;
    using AppearanceSystem3D = vr::ecs::AppearanceSystem<vr::ecs::Dim3>;
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
    using Coordinator3D = vr::render::AppearanceFrameCoordinator<vr::ecs::Dim3>;

    constexpr std::uint32_t appearance_count = 256U;
    constexpr std::uint32_t geometry_count = 1024U;
    AppearanceBenchMcVector<Appearance3D> appearance_components{};
    appearance_components.resize(appearance_count);
    AppearanceBenchMcVector<Geometry3D> geometry_components{};
    geometry_components.resize(geometry_count);

    for (std::uint32_t i = 0U; i < appearance_count; ++i) {
        AppearanceSystem3D::Initialize(appearance_components[i]);
        AppearanceSystem3D::SetBaseColorSurface(appearance_components[i], vr::render::MakeAppearanceSampledSurfaceHandle(100U + i));
        AppearanceSystem3D::SetNormalSurface(appearance_components[i], vr::render::MakeAppearanceSampledSurfaceHandle(400U + i));
        AppearanceSystem3D::SetSurfaceSamplerId(appearance_components[i], 3U);
    }

    for (std::uint32_t i = 0U; i < geometry_count; ++i) {
        GeometrySystem3D::Initialize(geometry_components[i]);
        GeometrySystem3D::SetGeometryId(geometry_components[i], 2000U + i);
        const std::uint32_t appearance_index = i % appearance_count;
        GeometrySystem3D::SetAppearanceHandle(geometry_components[i],
                                              appearance_components[appearance_index].runtime.gpu_record_handle);
    }

    Coordinator3D coordinator{};
    coordinator.SetAppearanceData(appearance_components.data(), appearance_count);
    coordinator.Reserve(appearance_count);
    (void)coordinator.PrepareFrame(0U);
    (void)coordinator.LinkGeometry(geometry_components.data(), geometry_count, 0U);

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const std::uint32_t dirty_appearance_index = static_cast<std::uint32_t>(i & (appearance_count - 1U));
        const std::int16_t layer = static_cast<std::int16_t>((i & 63U) - 32U);
        AppearanceSystem3D::SetLayer(appearance_components[dirty_appearance_index], layer);
        coordinator.SetDirtyHint(&dirty_appearance_index, 1U);

        const std::uint32_t frame_index = static_cast<std::uint32_t>(i + 1U);
        (void)coordinator.PrepareFrame(frame_index);
        const vr::ecs::AppearanceLinkStats link_stats =
            coordinator.LinkGeometry(geometry_components.data(), geometry_count, frame_index);
        vr::bench::BenchmarkContext::DoNotOptimize(link_stats.updated_count);
    }

    bench_context_.AddItems(iterations * geometry_count);
    bench_context_.AddBytes(iterations * geometry_count * sizeof(vr::ecs::GeometryRuntimeRoute));
    vr::bench::BenchmarkContext::DoNotOptimize(coordinator.Stats().geometry_link_incremental_call_count);
    vr::bench::BenchmarkContext::DoNotOptimize(coordinator.Stats().geometry_link_candidate_scan_count);
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(EcsAppearanceFrameCoordinator_dim3_link_incremental_idle_1k,
                  "core;ecs;appearance;link;cpu;coordinator;incremental;idle") {
    using Appearance3D = vr::ecs::Appearance<vr::ecs::Dim3>;
    using AppearanceSystem3D = vr::ecs::AppearanceSystem<vr::ecs::Dim3>;
    using Geometry3D = vr::ecs::Geometry<vr::ecs::Dim3>;
    using GeometrySystem3D = vr::ecs::GeometrySystem<vr::ecs::Dim3>;
    using Coordinator3D = vr::render::AppearanceFrameCoordinator<vr::ecs::Dim3>;

    constexpr std::uint32_t component_count = 1024U;
    AppearanceBenchMcVector<Appearance3D> appearance_components{};
    AppearanceBenchMcVector<Geometry3D> geometry_components{};
    appearance_components.resize(component_count);
    geometry_components.resize(component_count);

    for (std::uint32_t i = 0U; i < component_count; ++i) {
        AppearanceSystem3D::Initialize(appearance_components[i]);
        AppearanceSystem3D::SetBaseColorSurface(appearance_components[i], vr::render::MakeAppearanceSampledSurfaceHandle(100U + i));
        AppearanceSystem3D::SetNormalSurface(appearance_components[i], vr::render::MakeAppearanceSampledSurfaceHandle(1100U + i));
        AppearanceSystem3D::SetSurfaceSamplerId(appearance_components[i], 3U);

        GeometrySystem3D::Initialize(geometry_components[i]);
        GeometrySystem3D::SetGeometryId(geometry_components[i], 8000U + i);
        GeometrySystem3D::SetAppearanceHandle(geometry_components[i],
                                              appearance_components[i].runtime.gpu_record_handle);
    }

    Coordinator3D coordinator{};
    coordinator.SetAppearanceData(appearance_components.data(), component_count);
    coordinator.Reserve(component_count);
    (void)coordinator.PrepareFrame(0U);
    (void)coordinator.LinkGeometry(geometry_components.data(), component_count, 0U);

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const std::uint32_t frame_index = static_cast<std::uint32_t>(i + 1U);
        (void)coordinator.PrepareFrame(frame_index);
        const vr::ecs::AppearanceLinkStats link_stats =
            coordinator.LinkGeometry(geometry_components.data(), component_count, frame_index);
        vr::bench::BenchmarkContext::DoNotOptimize(link_stats.scanned_count);
        vr::bench::BenchmarkContext::DoNotOptimize(link_stats.updated_count);
    }

    bench_context_.AddItems(iterations * component_count);
    bench_context_.AddBytes(iterations * component_count * sizeof(vr::ecs::GeometryRuntimeRoute));
    vr::bench::BenchmarkContext::DoNotOptimize(coordinator.Stats().geometry_link_incremental_call_count);
    vr::bench::BenchmarkContext::DoNotOptimize(coordinator.Stats().geometry_link_candidate_scan_count);
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(EcsAppearanceFrameCoordinator_dim3_dirty_normalize_duplicate_payload_1k,
                  "core;ecs;appearance;prepare;cpu;coordinator;dirty_normalize") {
    using Appearance3D = vr::ecs::Appearance<vr::ecs::Dim3>;
    using AppearanceSystem3D = vr::ecs::AppearanceSystem<vr::ecs::Dim3>;
    using Coordinator3D = vr::render::AppearanceFrameCoordinator<vr::ecs::Dim3>;

    constexpr std::uint32_t component_count = 1024U;
    AppearanceBenchMcVector<Appearance3D> appearance_components{};
    appearance_components.resize(component_count);
    for (std::uint32_t i = 0U; i < component_count; ++i) {
        AppearanceSystem3D::Initialize(appearance_components[i]);
        AppearanceSystem3D::SetBaseColorSurface(appearance_components[i], vr::render::MakeAppearanceSampledSurfaceHandle(100U + (i % 128U)));
        AppearanceSystem3D::SetNormalSurface(appearance_components[i], vr::render::MakeAppearanceSampledSurfaceHandle(200U + (i % 128U)));
        AppearanceSystem3D::SetSurfaceSamplerId(appearance_components[i], 3U);
    }

    Coordinator3D coordinator{};
    coordinator.SetAppearanceData(appearance_components.data(), component_count);
    coordinator.Reserve(component_count);
    (void)coordinator.PrepareFrame(0U);

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const std::uint32_t hot_index = static_cast<std::uint32_t>(i & (component_count - 1U));
        const std::uint32_t next_index = (hot_index + 1U) & (component_count - 1U);
        AppearanceSystem3D::SetRoughness(appearance_components[hot_index],
                                         static_cast<float>((hot_index + static_cast<std::uint32_t>(i)) & 255U) / 255.0F);
        AppearanceSystem3D::SetMetallic(appearance_components[next_index],
                                        static_cast<float>((next_index + static_cast<std::uint32_t>(i * 3U)) & 255U) / 255.0F);

        const std::uint32_t dirty_payload[] = {
            hot_index, hot_index, next_index, next_index, hot_index, component_count + 17U
        };
        coordinator.SetDirtyHint(dirty_payload,
                                 static_cast<std::uint32_t>(sizeof(dirty_payload) / sizeof(dirty_payload[0])));
        (void)coordinator.PrepareFrame(static_cast<std::uint32_t>(i + 1U));
    }

    bench_context_.AddItems(iterations * component_count);
    bench_context_.AddBytes(iterations * component_count * sizeof(vr::ecs::AppearanceGpuRecord<vr::ecs::Dim3>));
    vr::bench::BenchmarkContext::DoNotOptimize(coordinator.Stats().dirty_hint_unique_count);
    vr::bench::BenchmarkContext::ClobberMemory();
}

} // namespace


