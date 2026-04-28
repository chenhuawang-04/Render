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
        AppearanceSystem2D::SetTextureBaseId(components[i], 1U + (i % 64U));
        AppearanceSystem2D::SetTextureMaskId(components[i], 11U + (i % 16U));
        AppearanceSystem2D::SetBindingLayoutId(components[i], 2U);
        AppearanceSystem2D::SetSamplerStateId(components[i], 3U);
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
        AppearanceSystem3D::SetTextureBaseColorId(components[i], 100U + (i % 128U));
        AppearanceSystem3D::SetTextureNormalId(components[i], 200U + (i % 128U));
        AppearanceSystem3D::SetTextureMetalRoughId(components[i], 300U + (i % 128U));
        AppearanceSystem3D::SetTextureOcclusionId(components[i], 400U + (i % 128U));
        AppearanceSystem3D::SetTextureEmissiveId(components[i], 500U + (i % 128U));
        AppearanceSystem3D::SetBindingLayoutId(components[i], 6U);
        AppearanceSystem3D::SetSamplerStateId(components[i], 4U);
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
        AppearanceSystem3D::SetTextureBaseColorId(appearance_components[i], 100U + (i % 128U));
        AppearanceSystem3D::SetTextureNormalId(appearance_components[i], 200U + (i % 128U));
        AppearanceSystem3D::SetBindingLayoutId(appearance_components[i], 3U);
        AppearanceSystem3D::SetSamplerStateId(appearance_components[i], 2U);
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
        AppearanceSystem3D::SetTextureBaseColorId(appearance_components[i], 100U + (i % 128U));
        AppearanceSystem3D::SetTextureNormalId(appearance_components[i], 200U + (i % 128U));
        AppearanceSystem3D::SetTextureMetalRoughId(appearance_components[i], 300U + (i % 64U));
        AppearanceSystem3D::SetBindingLayoutId(appearance_components[i], 5U);
        AppearanceSystem3D::SetSamplerStateId(appearance_components[i], 3U);

        GeometrySystem3D::Initialize(geometry_components[i]);
        GeometrySystem3D::SetGeometryId(geometry_components[i], 1U + i);

        SurfaceSystem3D::Initialize(surface_components[i]);
        SurfaceSystem3D::SetTextureId(surface_components[i], 500U + i);
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
        AppearanceSystem3D::SetTextureBaseColorId(appearance_components[i], 100U + (i % 128U));
        AppearanceSystem3D::SetTextureNormalId(appearance_components[i], 200U + (i % 128U));
        AppearanceSystem3D::SetTextureMetalRoughId(appearance_components[i], 300U + (i % 64U));
        AppearanceSystem3D::SetBindingLayoutId(appearance_components[i], 5U);
        AppearanceSystem3D::SetSamplerStateId(appearance_components[i], 3U);

        GeometrySystem3D::Initialize(geometry_components[i]);
        GeometrySystem3D::SetGeometryId(geometry_components[i], 1U + i);

        SurfaceSystem3D::Initialize(surface_components[i]);
        SurfaceSystem3D::SetTextureId(surface_components[i], 500U + i);
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

} // namespace
