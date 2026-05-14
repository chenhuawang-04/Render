#include "Center/Memory/Vulkan/Types.hpp"
#include "support/bench_framework.hpp"

#include <cstddef>
#include <cstdint>

namespace {

namespace VulkanTypes = Center::Memory::Vulkan;

VR_BENCHMARK_CASE(VulkanTypes_align_up_checked_hot_path, "core;memory;cpu") {
    std::size_t accumulator = 0U;
    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const std::size_t value = static_cast<std::size_t>((i * 13U) & 0x00FFFFFFU);
        accumulator ^= VulkanTypes::align_up_checked(value, 256U);
    }

    bench_context_.AddItems(iterations);
    bench_context_.AddBytes(iterations * sizeof(std::size_t));
    vr::bench::BenchmarkContext::DoNotOptimize(accumulator);
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(VulkanTypes_checked_add_overflow_guard, "core;memory;cpu") {
    std::size_t accumulator = 0U;
    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const std::size_t lhs = static_cast<std::size_t>((i * 97U) & 0x1FFFFFU);
        const std::size_t rhs = static_cast<std::size_t>((i * 31U) & 0x1FFFFFU);
        accumulator ^= VulkanTypes::checked_add(lhs, rhs);
    }

    bench_context_.AddItems(iterations);
    bench_context_.AddBytes(iterations * sizeof(std::size_t) * 2U);
    vr::bench::BenchmarkContext::DoNotOptimize(accumulator);
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(VulkanTypes_should_dedicate_allocation_policy_eval, "core;memory;cpu") {
    VulkanTypes::AllocationRequest request{};
    request.allocation_kind = VulkanTypes::AllocationKind::image_optimal;
    request.dedicated_preferred = true;
    request.host_access = VulkanTypes::HostAccess::random_read_write;
    request.lifetime_hint = VulkanTypes::LifetimeHint::long_lived;
    request.persistent_map = true;

    VulkanTypes::AllocationPolicy policy = VulkanTypes::AllocationPolicy::throughput_first();
    constexpr std::size_t default_block_bytes = 64U * 1024U * 1024U;

    std::uint64_t true_count = 0U;
    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const std::size_t request_size = static_cast<std::size_t>(4096U + (i & 0x1FFFFU));
        if (VulkanTypes::should_dedicate_allocation(request_size, default_block_bytes, request, policy)) {
            ++true_count;
        }
        request.persistent_map = !request.persistent_map;
    }

    bench_context_.AddItems(iterations);
    bench_context_.AddBytes(iterations * sizeof(std::size_t));
    vr::bench::BenchmarkContext::DoNotOptimize(true_count);
    vr::bench::BenchmarkContext::ClobberMemory();
}

} // namespace

