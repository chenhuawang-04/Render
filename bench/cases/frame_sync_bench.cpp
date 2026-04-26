#include "support/bench_framework.hpp"
#include "vr/render/frame_sync_host.hpp"

#include <cstdint>

namespace {

VR_BENCHMARK_CASE(FrameSyncHost_advance_frame_ring, "core;render;cpu") {
    vr::render::FrameSyncHost<3U> frame_sync{};
    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        frame_sync.AdvanceFrame();
    }

    bench_context_.AddItems(iterations);
    bench_context_.AddBytes(iterations * sizeof(std::uint32_t));
    vr::bench::BenchmarkContext::DoNotOptimize(frame_sync.CurrentFrameIndex());
    vr::bench::BenchmarkContext::ClobberMemory();
}

} // namespace
