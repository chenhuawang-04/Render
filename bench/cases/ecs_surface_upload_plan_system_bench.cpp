#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/system/surface_runtime_system.hpp"
#include "vr/ecs/system/surface_system.hpp"
#include "vr/ecs/system/surface_upload_plan_system.hpp"
#include "vr/ecs/system/transform_system.hpp"

#include "support/bench_framework.hpp"

#include <array>
#include <cstdint>

namespace {

using Surface3D = vr::ecs::Surface<vr::ecs::Dim3>;
using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
using SurfaceSystem3D = vr::ecs::SurfaceSystem<vr::ecs::Dim3>;
using SurfaceRuntimeSystem3D = vr::ecs::SurfaceRuntimeSystem<vr::ecs::Dim3>;
using SurfaceUploadPlanSystem3D = vr::ecs::SurfaceUploadPlanSystem<vr::ecs::Dim3>;
using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;

template<typename T>
using SurfaceBenchMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct SurfacePlanBenchData final {
    SurfaceBenchMcVector<Surface3D> surfaces{};
    SurfaceBenchMcVector<Transform3D> transforms{};
    vr::ecs::Surface3DRuntimeScratch runtime_scratch{};
    vr::ecs::SurfaceUploadPlanScratch<vr::ecs::Dim3> plan_scratch{};
};

void InitializeBenchData(SurfacePlanBenchData& data_, std::uint32_t component_count_) {
    data_.surfaces.resize(component_count_);
    data_.transforms.resize(component_count_);

    for (std::uint32_t i = 0U; i < component_count_; ++i) {
        SurfaceSystem3D::Initialize(data_.surfaces[i]);
        SurfaceSystem3D::SetTextureRoute(data_.surfaces[i], 10000U + i, 17U, 0U, 0U);
        SurfaceSystem3D::SetRuntimeRoute(data_.surfaces[i], 10000U + i, 7U + (i & 0x7U), i & 0x3U);
        SurfaceSystem3D::SetDepthBin(data_.surfaces[i], static_cast<std::uint16_t>(i & 0x3FU));

        TransformSystem3D::Initialize(data_.transforms[i]);
        TransformSystem3D::SetLocalPosition(data_.transforms[i],
                                            vr::ecs::Float3{
                                                .x = static_cast<float>(i & 63U) * 0.25F,
                                                .y = static_cast<float>((i >> 6U) & 63U) * 0.25F,
                                                .z = -0.2F * static_cast<float>(i & 0xFU)});
    }

    TransformSystem3D::UpdateHierarchy(data_.transforms.data(),
                                       static_cast<std::uint32_t>(data_.transforms.size()));

    const auto runtime_stats = SurfaceRuntimeSystem3D::Build(data_.surfaces.data(),
                                                              data_.transforms.data(),
                                                              static_cast<std::uint32_t>(data_.surfaces.size()),
                                                              data_.runtime_scratch);
    if (runtime_stats.emitted_instance_count == 0U) {
        VR_BENCH_SKIP("Surface runtime produced zero instances");
    }
}

VR_BENCHMARK_CASE(EcsSurfaceUploadPlan_dim3_sparse_4k, "perf;ecs;surface;surface-upload-plan") {
    constexpr std::size_t component_count = 4096U;
    SurfacePlanBenchData data{};
    InitializeBenchData(data, static_cast<std::uint32_t>(component_count));

    std::array<std::uint32_t, 64U> dirty_components{};
    for (std::uint32_t i = 0U; i < dirty_components.size(); ++i) {
        dirty_components[i] = (i * 61U + 7U) & (component_count - 1U);
    }

    bench_context_.ForEachIteration([&](std::uint64_t iter_) {
        (void)iter_;
        const auto stats = SurfaceUploadPlanSystem3D::BuildRangesFromDirtyComponents(
            data.runtime_scratch,
            dirty_components.data(),
            static_cast<std::uint32_t>(dirty_components.size()),
            data.plan_scratch);
        bench_context_.AddItems(stats.resolved_instance_count);
        bench_context_.AddBytes(static_cast<std::uint64_t>(stats.resolved_instance_count) *
                                sizeof(vr::ecs::Surface3DGpuInstance));
        vr::bench::BenchmarkContext::DoNotOptimize(stats.range_count);
        vr::bench::BenchmarkContext::ClobberMemory();
    });
}

VR_BENCHMARK_CASE(EcsSurfaceUploadPlan_dim3_sparse_4k_gap_merge_1, "perf;ecs;surface;surface-upload-plan") {
    constexpr std::size_t component_count = 4096U;
    SurfacePlanBenchData data{};
    InitializeBenchData(data, static_cast<std::uint32_t>(component_count));

    std::array<std::uint32_t, 256U> dirty_components{};
    for (std::uint32_t i = 0U; i < dirty_components.size(); ++i) {
        dirty_components[i] = (i * 13U) & (component_count - 1U);
    }

    const vr::ecs::SurfaceUploadPlanBuildOptions options{
        .merge_gap_instances = 1U,
        .dense_path_min_dirty_count = 64U,
        .dense_path_min_coverage_percent = 25U
    };

    bench_context_.ForEachIteration([&](std::uint64_t iter_) {
        (void)iter_;
        const auto stats = SurfaceUploadPlanSystem3D::BuildRangesFromDirtyComponents(
            data.runtime_scratch,
            dirty_components.data(),
            static_cast<std::uint32_t>(dirty_components.size()),
            options,
            data.plan_scratch);
        bench_context_.AddItems(stats.covered_instance_count);
        bench_context_.AddBytes(static_cast<std::uint64_t>(stats.covered_instance_count) *
                                sizeof(vr::ecs::Surface3DGpuInstance));
        vr::bench::BenchmarkContext::DoNotOptimize(stats.range_count);
        vr::bench::BenchmarkContext::DoNotOptimize(stats.merged_gap_instance_count);
        vr::bench::BenchmarkContext::ClobberMemory();
    });
}

VR_BENCHMARK_CASE(EcsSurfaceUploadPlan_dim3_dense_4k, "perf;ecs;surface;surface-upload-plan") {
    constexpr std::size_t component_count = 4096U;
    SurfacePlanBenchData data{};
    InitializeBenchData(data, static_cast<std::uint32_t>(component_count));

    std::array<std::uint32_t, 3072U> dirty_components{};
    for (std::uint32_t i = 0U; i < dirty_components.size(); ++i) {
        dirty_components[i] = i;
    }

    bench_context_.ForEachIteration([&](std::uint64_t iter_) {
        (void)iter_;
        const auto stats = SurfaceUploadPlanSystem3D::BuildRangesFromDirtyComponents(
            data.runtime_scratch,
            dirty_components.data(),
            static_cast<std::uint32_t>(dirty_components.size()),
            data.plan_scratch);
        bench_context_.AddItems(stats.resolved_instance_count);
        bench_context_.AddBytes(static_cast<std::uint64_t>(stats.resolved_instance_count) *
                                sizeof(vr::ecs::Surface3DGpuInstance));
        vr::bench::BenchmarkContext::DoNotOptimize(stats.used_dense_path);
        vr::bench::BenchmarkContext::DoNotOptimize(stats.range_count);
        vr::bench::BenchmarkContext::ClobberMemory();
    });
}

} // namespace
