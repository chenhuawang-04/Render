#include "support/test_framework.hpp"
#include "vr/render/frame_retire_host.hpp"

#include <cstdint>
#include <limits>

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

VR_TEST_CASE(FrameRetireHost_retire_ignores_null_handles, "unit;core;render") {
    vr::render::FrameRetireHost retire_host{};

    retire_host.RetireImageView(VK_NULL_HANDLE, 1U);
    retire_host.RetireFramebuffer(VK_NULL_HANDLE, 1U);
    retire_host.RetireSwapchain(VK_NULL_HANDLE, 1U);
    retire_host.RetireCommandPool(VK_NULL_HANDLE, 1U);

    VR_CHECK(retire_host.Empty());
    VR_CHECK(retire_host.PendingImageViews() == 0U);
    VR_CHECK(retire_host.PendingFramebuffers() == 0U);
    VR_CHECK(retire_host.PendingSwapchains() == 0U);
    VR_CHECK(retire_host.PendingCommandPools() == 0U);
}

VR_TEST_CASE(FrameRetireHost_flush_with_null_device_clears_pending_queues, "unit;core;render") {
    vr::render::FrameRetireHost retire_host{};

    retire_host.RetireImageView(FakeImageView(1U), 5U);
    retire_host.RetireImageView(FakeImageView(2U), 6U);
    retire_host.RetireFramebuffer(FakeFramebuffer(3U), 7U);
    retire_host.RetireSwapchain(FakeSwapchain(4U), 8U);
    retire_host.RetireCommandPool(FakeCommandPool(5U), 9U);

    VR_CHECK(retire_host.PendingImageViews() == 2U);
    VR_CHECK(retire_host.PendingFramebuffers() == 1U);
    VR_CHECK(retire_host.PendingSwapchains() == 1U);
    VR_CHECK(retire_host.PendingCommandPools() == 1U);

    const vr::render::FrameRetireStats stats = retire_host.Flush(VK_NULL_HANDLE);
    VR_CHECK(stats.destroyed_image_views == 0U);
    VR_CHECK(stats.destroyed_framebuffers == 0U);
    VR_CHECK(stats.destroyed_swapchains == 0U);
    VR_CHECK(stats.destroyed_command_pools == 0U);

    VR_CHECK(retire_host.Empty());
    VR_CHECK(retire_host.PendingImageViews() == 0U);
    VR_CHECK(retire_host.PendingFramebuffers() == 0U);
    VR_CHECK(retire_host.PendingSwapchains() == 0U);
    VR_CHECK(retire_host.PendingCommandPools() == 0U);
}

VR_TEST_CASE(FrameRetireHost_collect_with_null_device_preserves_pending_state, "unit;core;render") {
    vr::render::FrameRetireHost retire_host{};
    retire_host.RetireImageView(FakeImageView(101U), 1U);
    retire_host.RetireFramebuffer(FakeFramebuffer(201U), 2U);
    retire_host.RetireSwapchain(FakeSwapchain(301U), 3U);
    retire_host.RetireCommandPool(FakeCommandPool(401U), 4U);

    const vr::render::FrameRetireStats collect_stats = retire_host.Collect(
        VK_NULL_HANDLE,
        /*completed_value_=*/std::numeric_limits<std::uint64_t>::max(),
        /*max_destroy_per_type_=*/1U);
    VR_CHECK(collect_stats.destroyed_image_views == 0U);
    VR_CHECK(collect_stats.destroyed_framebuffers == 0U);
    VR_CHECK(collect_stats.destroyed_swapchains == 0U);
    VR_CHECK(collect_stats.destroyed_command_pools == 0U);

    VR_CHECK(retire_host.PendingImageViews() == 1U);
    VR_CHECK(retire_host.PendingFramebuffers() == 1U);
    VR_CHECK(retire_host.PendingSwapchains() == 1U);
    VR_CHECK(retire_host.PendingCommandPools() == 1U);
    VR_CHECK(!retire_host.Empty());

    (void)retire_host.Flush(VK_NULL_HANDLE);
    VR_CHECK(retire_host.Empty());
}

} // namespace

