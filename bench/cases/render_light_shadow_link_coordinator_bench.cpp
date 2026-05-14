#include "support/bench_framework.hpp"
#include "vr/render/light_shadow_link_coordinator.hpp"

#include <cstdint>

namespace {

template<typename T>
using LinkBenchMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

VR_BENCHMARK_CASE(RenderLightShadowLinkCoordinator3D_build_4k_lights_2k_shadows,
                  "core;render;light;shadow;link;cpu") {
    using Coordinator = vr::render::LightShadowLinkCoordinator3D;
    using PrepareInfo = vr::render::LightShadowLinkCoordinator3DPrepareInfo;
    using LightRecord3D = vr::ecs::LightGpuRecord3D;
    using Shadow3D = vr::ecs::Shadow<vr::ecs::Dim3>;
    using ShadowRecord3D = vr::ecs::ShadowGpuRecord3D;

    constexpr std::uint32_t light_count = 4096U;
    constexpr std::uint32_t shadow_count = 2048U;

    LinkBenchMcVector<LightRecord3D> light_records{};
    light_records.resize(light_count);
    LinkBenchMcVector<Shadow3D> shadow_components{};
    shadow_components.resize(shadow_count);
    LinkBenchMcVector<ShadowRecord3D> shadow_records{};
    shadow_records.resize(shadow_count);

    for (std::uint32_t i = 0U; i < shadow_count; ++i) {
        shadow_components[i].visibility.enabled = 1U;
        shadow_components[i].visibility.visible = 1U;
        shadow_components[i].binding.light_component_index = i % light_count;
        shadow_components[i].binding.atlas_namespace_id = 1U;

        shadow_records[i].first_view_index = i;
        shadow_records[i].view_count = 1U;
        shadow_records[i].projection_kind =
            static_cast<std::uint32_t>(vr::ecs::ShadowProjectionKind::directional);
    }

    Coordinator coordinator{};
    coordinator.Reserve(light_count);

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        PrepareInfo prepare_info{};
        prepare_info.signature = 0x10000000ULL + i;
        prepare_info.light_records = light_records.data();
        prepare_info.light_record_count = light_count;
        prepare_info.shadow_components = shadow_components.data();
        prepare_info.shadow_component_count = shadow_count;
        prepare_info.shadow_records = shadow_records.data();
        prepare_info.shadow_record_count = shadow_count;
        prepare_info.shadow_namespace_hint = 0U;
        const auto result = coordinator.Prepare(prepare_info);
        vr::bench::BenchmarkContext::DoNotOptimize(result.link_result.linked_light_count);
    }

    bench_context_.AddItems(iterations * light_count);
    bench_context_.AddBytes(iterations * light_count * sizeof(LightRecord3D));
    vr::bench::BenchmarkContext::DoNotOptimize(coordinator.Stats().build_count);
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(RenderLightShadowLinkCoordinator3D_reuse_hot_path_4k_lights_2k_shadows,
                  "core;render;light;shadow;link;cpu;reuse") {
    using Coordinator = vr::render::LightShadowLinkCoordinator3D;
    using PrepareInfo = vr::render::LightShadowLinkCoordinator3DPrepareInfo;
    using LightRecord3D = vr::ecs::LightGpuRecord3D;
    using Shadow3D = vr::ecs::Shadow<vr::ecs::Dim3>;
    using ShadowRecord3D = vr::ecs::ShadowGpuRecord3D;

    constexpr std::uint32_t light_count = 4096U;
    constexpr std::uint32_t shadow_count = 2048U;

    LinkBenchMcVector<LightRecord3D> light_records{};
    light_records.resize(light_count);
    LinkBenchMcVector<Shadow3D> shadow_components{};
    shadow_components.resize(shadow_count);
    LinkBenchMcVector<ShadowRecord3D> shadow_records{};
    shadow_records.resize(shadow_count);

    for (std::uint32_t i = 0U; i < shadow_count; ++i) {
        shadow_components[i].visibility.enabled = 1U;
        shadow_components[i].visibility.visible = 1U;
        shadow_components[i].binding.light_component_index = i % light_count;
        shadow_components[i].binding.atlas_namespace_id = 1U;

        shadow_records[i].first_view_index = i;
        shadow_records[i].view_count = 1U;
        shadow_records[i].projection_kind =
            static_cast<std::uint32_t>(vr::ecs::ShadowProjectionKind::spot);
    }

    Coordinator coordinator{};
    coordinator.Reserve(light_count);

    PrepareInfo prepare_info{};
    prepare_info.signature = 0x42424242ULL;
    prepare_info.light_records = light_records.data();
    prepare_info.light_record_count = light_count;
    prepare_info.shadow_components = shadow_components.data();
    prepare_info.shadow_component_count = shadow_count;
    prepare_info.shadow_records = shadow_records.data();
    prepare_info.shadow_record_count = shadow_count;
    prepare_info.shadow_namespace_hint = 0U;
    (void)coordinator.Prepare(prepare_info);

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const auto result = coordinator.Prepare(prepare_info);
        vr::bench::BenchmarkContext::DoNotOptimize(result.link_result.linked_light_count);
    }

    bench_context_.AddItems(iterations * light_count);
    bench_context_.AddBytes(iterations * sizeof(std::uint64_t));
    vr::bench::BenchmarkContext::DoNotOptimize(coordinator.Stats().cache_reuse_hit_count);
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(RenderLightShadowLinkCoordinator3D_incremental_patch_sparse_4k_lights_2k_shadows,
                  "core;render;light;shadow;link;cpu;incremental") {
    using Coordinator = vr::render::LightShadowLinkCoordinator3D;
    using PrepareInfo = vr::render::LightShadowLinkCoordinator3DPrepareInfo;
    using LightRecord3D = vr::ecs::LightGpuRecord3D;
    using Shadow3D = vr::ecs::Shadow<vr::ecs::Dim3>;
    using ShadowRecord3D = vr::ecs::ShadowGpuRecord3D;

    constexpr std::uint32_t light_count = 4096U;
    constexpr std::uint32_t shadow_count = 2048U;

    LinkBenchMcVector<LightRecord3D> light_records{};
    light_records.resize(light_count);
    for (std::uint32_t i = 0U; i < light_count; ++i) {
        light_records[i].intensity = static_cast<float>((i % 17U) + 1U);
    }

    LinkBenchMcVector<Shadow3D> shadow_components{};
    shadow_components.resize(shadow_count);
    LinkBenchMcVector<ShadowRecord3D> shadow_records{};
    shadow_records.resize(shadow_count);

    for (std::uint32_t i = 0U; i < shadow_count; ++i) {
        shadow_components[i].visibility.enabled = 1U;
        shadow_components[i].visibility.visible = 1U;
        shadow_components[i].binding.light_component_index = i % light_count;
        shadow_components[i].binding.atlas_namespace_id = 1U;

        shadow_records[i].first_view_index = i;
        shadow_records[i].view_count = 1U;
        shadow_records[i].projection_kind =
            static_cast<std::uint32_t>(vr::ecs::ShadowProjectionKind::spot);
    }

    Coordinator coordinator{};
    coordinator.Reserve(light_count);

    PrepareInfo base_prepare_info{};
    base_prepare_info.signature = 0x90000000ULL;
    base_prepare_info.light_signature = 0x11111111ULL;
    base_prepare_info.shadow_signature = 0x22222222ULL;
    base_prepare_info.light_records = light_records.data();
    base_prepare_info.light_record_count = light_count;
    base_prepare_info.shadow_components = shadow_components.data();
    base_prepare_info.shadow_component_count = shadow_count;
    base_prepare_info.shadow_records = shadow_records.data();
    base_prepare_info.shadow_record_count = shadow_count;
    base_prepare_info.shadow_namespace_hint = 0U;
    (void)coordinator.Prepare(base_prepare_info);

    std::uint32_t updated_light_index = 0U;
    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        updated_light_index = static_cast<std::uint32_t>(i % static_cast<std::uint64_t>(light_count));
        light_records[updated_light_index].intensity += 0.125F;
        const std::uint32_t updated_indices[1U] = {updated_light_index};

        PrepareInfo prepare_info = base_prepare_info;
        prepare_info.signature = 0x90000001ULL + i;
        prepare_info.light_signature = 0x11111112ULL + i;
        prepare_info.shadow_signature = base_prepare_info.shadow_signature;
        prepare_info.light_updated_component_indices = updated_indices;
        prepare_info.light_updated_component_count = 1U;
        prepare_info.allow_incremental_light_patch = 1U;

        const auto result = coordinator.Prepare(prepare_info);
        vr::bench::BenchmarkContext::DoNotOptimize(result.link_result.linked_light_records);
        vr::bench::BenchmarkContext::DoNotOptimize(result.link_result.linked_light_records[updated_light_index].intensity);
    }

    bench_context_.AddItems(iterations);
    bench_context_.AddBytes(iterations * sizeof(LightRecord3D));
    vr::bench::BenchmarkContext::DoNotOptimize(coordinator.Stats().incremental_patch_count);
    vr::bench::BenchmarkContext::ClobberMemory();
}

} // namespace

