#include "support/bench_framework.hpp"
#include "vr/render/render_runtime_host.hpp"
#include "vr/runtime/runtime_diagnostics.hpp"
#include "vr/runtime/services/frame_composer_service.hpp"
#include "vr/runtime/services/particle_render_service.hpp"
#include "vr/runtime/services/particle_simulation_service.hpp"

#include <array>
#include <cstdint>

namespace {

using Runtime = vr::render::RenderRuntimeHost<vr::platform::ActiveBackendTag, 2U>;

VR_BENCHMARK_CASE(RuntimeDiagnostics_level_predicates, "core;runtime;cpu") {
    constexpr std::array<vr::runtime::DiagnosticsLevel, 5U> k_levels{
        vr::runtime::DiagnosticsLevel::Off,
        vr::runtime::DiagnosticsLevel::CountersOnly,
        vr::runtime::DiagnosticsLevel::Detailed,
        vr::runtime::DiagnosticsLevel::GpuTiming,
        vr::runtime::DiagnosticsLevel::Capture,
    };

    std::uint64_t score = 0U;
    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const auto level = k_levels[static_cast<std::size_t>(i % k_levels.size())];
        score += vr::runtime::DiagnosticsCollectsFrameData(level) ? 1U : 0U;
        score += vr::runtime::DiagnosticsCollectsServiceCounters(level) ? 1U : 0U;
        score += vr::runtime::DiagnosticsCollectsDetailedData(level) ? 1U : 0U;
    }

    bench_context_.AddItems(iterations);
    bench_context_.AddBytes(iterations * sizeof(vr::runtime::DiagnosticsLevel));
    vr::bench::BenchmarkContext::DoNotOptimize(score);
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(RuntimeServices_profiled_try_get_particle_services, "core;runtime;cpu") {
    Runtime runtime{};
    auto& services = runtime.Services();

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        auto* particle_render =
            services.TryGet<vr::runtime::services::ParticleRenderService>();
        auto* particle_simulation =
            services.TryGet<vr::runtime::services::ParticleSimulationService>();
        auto* frame_composer =
            services.TryGet<vr::runtime::services::FrameComposerService>();
        vr::bench::BenchmarkContext::DoNotOptimize(particle_render);
        vr::bench::BenchmarkContext::DoNotOptimize(particle_simulation);
        vr::bench::BenchmarkContext::DoNotOptimize(frame_composer);
    }

    bench_context_.AddItems(iterations * 3U);
    bench_context_.AddBytes(iterations * sizeof(void*) * 3U);
    vr::bench::BenchmarkContext::ClobberMemory();
}

} // namespace
