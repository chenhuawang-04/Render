#include "support/bench_framework.hpp"
#include "vr/render/frame_retire_host.hpp"

#include <cstdint>

namespace {

[[nodiscard]] VkImageView FakeImageView(std::uint64_t value_) {
    return reinterpret_cast<VkImageView>(static_cast<std::uintptr_t>(value_));
}

[[nodiscard]] VkFramebuffer FakeFramebuffer(std::uint64_t value_) {
    return reinterpret_cast<VkFramebuffer>(static_cast<std::uintptr_t>(value_));
}

[[nodiscard]] VkSwapchainKHR FakeSwapchain(std::uint64_t value_) {
    return reinterpret_cast<VkSwapchainKHR>(static_cast<std::uintptr_t>(value_));
}

[[nodiscard]] VkCommandPool FakeCommandPool(std::uint64_t value_) {
    return reinterpret_cast<VkCommandPool>(static_cast<std::uintptr_t>(value_));
}

VR_BENCHMARK_CASE(FrameRetireHost_enqueue_image_view_only, "core;render;cpu") {
    vr::render::FrameRetireHost retire_host{};
    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        retire_host.RetireImageView(FakeImageView(i + 1U), i + 1U);
    }

    bench_context_.AddItems(iterations);
    bench_context_.AddBytes(iterations * sizeof(std::uint64_t));
    vr::bench::BenchmarkContext::DoNotOptimize(retire_host.PendingImageViews());

    const vr::render::FrameRetireStats flush_stats = retire_host.Flush(VK_NULL_HANDLE);
    vr::bench::BenchmarkContext::DoNotOptimize(flush_stats.destroyed_image_views);
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(FrameRetireHost_enqueue_mixed_handles, "core;render;cpu") {
    vr::render::FrameRetireHost retire_host{};
    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const std::uint64_t base = i * 4U + 1U;
        retire_host.RetireImageView(FakeImageView(base + 0U), i);
        retire_host.RetireFramebuffer(FakeFramebuffer(base + 1U), i);
        retire_host.RetireSwapchain(FakeSwapchain(base + 2U), i);
        retire_host.RetireCommandPool(FakeCommandPool(base + 3U), i);
    }

    bench_context_.AddItems(iterations * 4U);
    bench_context_.AddBytes(iterations * sizeof(std::uint64_t) * 4U);

    vr::bench::BenchmarkContext::DoNotOptimize(retire_host.PendingImageViews());
    vr::bench::BenchmarkContext::DoNotOptimize(retire_host.PendingFramebuffers());
    vr::bench::BenchmarkContext::DoNotOptimize(retire_host.PendingSwapchains());
    vr::bench::BenchmarkContext::DoNotOptimize(retire_host.PendingCommandPools());

    const vr::render::FrameRetireStats flush_stats = retire_host.Flush(VK_NULL_HANDLE);
    vr::bench::BenchmarkContext::DoNotOptimize(flush_stats.destroyed_framebuffers);
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(FrameRetireHost_collect_null_device_fast_reject, "core;render;cpu") {
    const std::uint64_t iterations = bench_context_.Iterations();
    std::uint64_t pending_sum = 0U;

    for (std::uint64_t i = 0U; i < iterations; ++i) {
        vr::render::FrameRetireHost retire_host{};
        retire_host.RetireImageView(FakeImageView(i + 1U), i);
        retire_host.RetireFramebuffer(FakeFramebuffer(i + 2U), i);
        (void)retire_host.Collect(VK_NULL_HANDLE, i, 1U);
        pending_sum += retire_host.PendingImageViews();
        pending_sum += retire_host.PendingFramebuffers();
    }

    bench_context_.AddItems(iterations);
    bench_context_.AddBytes(iterations * sizeof(std::uint64_t) * 2U);
    vr::bench::BenchmarkContext::DoNotOptimize(pending_sum);
    vr::bench::BenchmarkContext::ClobberMemory();
}

} // namespace

