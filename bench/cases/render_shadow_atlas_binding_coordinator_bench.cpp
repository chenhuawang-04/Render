#include "support/bench_framework.hpp"
#include "vr/render/shadow_atlas_binding_coordinator.hpp"

#include <cstdint>

namespace {

VR_BENCHMARK_CASE(RenderShadowAtlasBindingCoordinator_fallback_build_path,
                  "core;render;shadow;atlas;binding;cpu") {
    vr::render::ShadowAtlasBindingCoordinator coordinator{};
    vr::render::ShadowAtlasBindingResolveInput resolve_input{};
    resolve_input.atlas_host = nullptr;
    resolve_input.namespace_id = 0U;
    resolve_input.fallback_namespace_id = 1U;
    resolve_input.allow_namespace_fallback = 1U;
    resolve_input.primary_sampler = VK_NULL_HANDLE;
    resolve_input.fallback_view = reinterpret_cast<VkImageView>(0x1000U);
    resolve_input.fallback_sampler = reinterpret_cast<VkSampler>(0x2000U);
    resolve_input.fallback_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        resolve_input.namespace_id = static_cast<std::uint32_t>(i & 1023ULL);
        const auto result = coordinator.Resolve(resolve_input);
        vr::bench::BenchmarkContext::DoNotOptimize(result.binding_signature);
    }

    bench_context_.AddItems(iterations);
    bench_context_.AddBytes(iterations * sizeof(vr::render::ShadowAtlasBindingResolveInput));
    vr::bench::BenchmarkContext::DoNotOptimize(coordinator.Stats().cache_build_count);
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(RenderShadowAtlasBindingCoordinator_fallback_reuse_hot_path,
                  "core;render;shadow;atlas;binding;cpu;reuse") {
    vr::render::ShadowAtlasBindingCoordinator coordinator{};
    vr::render::ShadowAtlasBindingResolveInput resolve_input{};
    resolve_input.atlas_host = nullptr;
    resolve_input.namespace_id = 33U;
    resolve_input.fallback_namespace_id = 1U;
    resolve_input.allow_namespace_fallback = 1U;
    resolve_input.primary_sampler = VK_NULL_HANDLE;
    resolve_input.fallback_view = reinterpret_cast<VkImageView>(0x3000U);
    resolve_input.fallback_sampler = reinterpret_cast<VkSampler>(0x4000U);
    resolve_input.fallback_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    (void)coordinator.Resolve(resolve_input);

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const auto result = coordinator.Resolve(resolve_input);
        vr::bench::BenchmarkContext::DoNotOptimize(result.binding_signature);
    }

    bench_context_.AddItems(iterations);
    bench_context_.AddBytes(iterations * sizeof(std::uint64_t));
    vr::bench::BenchmarkContext::DoNotOptimize(coordinator.Stats().cache_reuse_hit_count);
    vr::bench::BenchmarkContext::ClobberMemory();
}

} // namespace

