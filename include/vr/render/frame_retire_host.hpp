#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vulkan/vulkan.h>

namespace vr::render {

template<typename T>
using RetireMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct FrameRetireStats {
    uint32_t destroyed_image_views = 0U;
    uint32_t destroyed_framebuffers = 0U;
    uint32_t destroyed_swapchains = 0U;
    uint32_t destroyed_command_pools = 0U;
};

class FrameRetireHost final {
public:
    FrameRetireHost() = default;
    ~FrameRetireHost() = default;

    FrameRetireHost(const FrameRetireHost&) = delete;
    FrameRetireHost& operator=(const FrameRetireHost&) = delete;

    FrameRetireHost(FrameRetireHost&&) = delete;
    FrameRetireHost& operator=(FrameRetireHost&&) = delete;

    void RetireImageView(VkImageView image_view_, uint64_t retire_value_) {
        if (image_view_ == VK_NULL_HANDLE) {
            return;
        }
        image_views.push_back({.retire_value = retire_value_, .handle = image_view_});
    }

    void RetireFramebuffer(VkFramebuffer framebuffer_, uint64_t retire_value_) {
        if (framebuffer_ == VK_NULL_HANDLE) {
            return;
        }
        framebuffers.push_back({.retire_value = retire_value_, .handle = framebuffer_});
    }

    void RetireSwapchain(VkSwapchainKHR swapchain_, uint64_t retire_value_) {
        if (swapchain_ == VK_NULL_HANDLE) {
            return;
        }
        swapchains.push_back({.retire_value = retire_value_, .handle = swapchain_});
    }

    void RetireCommandPool(VkCommandPool command_pool_, uint64_t retire_value_) {
        if (command_pool_ == VK_NULL_HANDLE) {
            return;
        }
        command_pools.push_back({.retire_value = retire_value_, .handle = command_pool_});
    }

    [[nodiscard]] FrameRetireStats Collect(VkDevice device_,
                                           uint64_t completed_value_,
                                           uint32_t max_destroy_per_type_ = std::numeric_limits<uint32_t>::max()) {
        if (device_ == VK_NULL_HANDLE) {
            return {};
        }

        FrameRetireStats stats{};
        stats.destroyed_image_views = CollectTyped<RetiredImageView>(
            image_views,
            completed_value_,
            max_destroy_per_type_,
            [&](VkImageView handle_) { vkDestroyImageView(device_, handle_, nullptr); });

        stats.destroyed_framebuffers = CollectTyped<RetiredFramebuffer>(
            framebuffers,
            completed_value_,
            max_destroy_per_type_,
            [&](VkFramebuffer handle_) { vkDestroyFramebuffer(device_, handle_, nullptr); });

        stats.destroyed_swapchains = CollectTyped<RetiredSwapchain>(
            swapchains,
            completed_value_,
            max_destroy_per_type_,
            [&](VkSwapchainKHR handle_) { vkDestroySwapchainKHR(device_, handle_, nullptr); });

        stats.destroyed_command_pools = CollectTyped<RetiredCommandPool>(
            command_pools,
            completed_value_,
            max_destroy_per_type_,
            [&](VkCommandPool handle_) { vkDestroyCommandPool(device_, handle_, nullptr); });

        return stats;
    }

    [[nodiscard]] FrameRetireStats Flush(VkDevice device_) {
        if (device_ == VK_NULL_HANDLE) {
            image_views.clear();
            framebuffers.clear();
            swapchains.clear();
            command_pools.clear();
            return {};
        }

        FrameRetireStats stats{};

        for (const auto& item : image_views) {
            vkDestroyImageView(device_, item.handle, nullptr);
            ++stats.destroyed_image_views;
        }
        image_views.clear();

        for (const auto& item : framebuffers) {
            vkDestroyFramebuffer(device_, item.handle, nullptr);
            ++stats.destroyed_framebuffers;
        }
        framebuffers.clear();

        for (const auto& item : swapchains) {
            vkDestroySwapchainKHR(device_, item.handle, nullptr);
            ++stats.destroyed_swapchains;
        }
        swapchains.clear();

        for (const auto& item : command_pools) {
            vkDestroyCommandPool(device_, item.handle, nullptr);
            ++stats.destroyed_command_pools;
        }
        command_pools.clear();

        return stats;
    }

    [[nodiscard]] bool Empty() const noexcept {
        return image_views.empty() &&
               framebuffers.empty() &&
               swapchains.empty() &&
               command_pools.empty();
    }

    [[nodiscard]] uint32_t PendingImageViews() const noexcept {
        return static_cast<uint32_t>(image_views.size());
    }

    [[nodiscard]] uint32_t PendingFramebuffers() const noexcept {
        return static_cast<uint32_t>(framebuffers.size());
    }

    [[nodiscard]] uint32_t PendingSwapchains() const noexcept {
        return static_cast<uint32_t>(swapchains.size());
    }

    [[nodiscard]] uint32_t PendingCommandPools() const noexcept {
        return static_cast<uint32_t>(command_pools.size());
    }

private:
    template<typename HandleT>
    struct RetiredHandle {
        uint64_t retire_value = 0U;
        HandleT handle = VK_NULL_HANDLE;
    };

    using RetiredImageView = RetiredHandle<VkImageView>;
    using RetiredFramebuffer = RetiredHandle<VkFramebuffer>;
    using RetiredSwapchain = RetiredHandle<VkSwapchainKHR>;
    using RetiredCommandPool = RetiredHandle<VkCommandPool>;

    template<typename NodeT, typename DestroyFn>
    static uint32_t CollectTyped(RetireMcVector<NodeT>& items_,
                                 uint64_t completed_value_,
                                 uint32_t max_destroy_,
                                 DestroyFn destroy_fn_) {
        if (items_.empty()) {
            return 0U;
        }

        uint32_t destroyed = 0U;
        uint32_t write_index = 0U;
        for (uint32_t read_index = 0U; read_index < items_.size(); ++read_index) {
            NodeT item = items_[read_index];
            const bool can_destroy = item.retire_value <= completed_value_ && destroyed < max_destroy_;
            if (can_destroy) {
                destroy_fn_(item.handle);
                ++destroyed;
            } else {
                if (write_index != read_index) {
                    items_[write_index] = item;
                }
                ++write_index;
            }
        }
        items_.resize(write_index);
        return destroyed;
    }

private:
    RetireMcVector<RetiredImageView> image_views{};
    RetireMcVector<RetiredFramebuffer> framebuffers{};
    RetireMcVector<RetiredSwapchain> swapchains{};
    RetireMcVector<RetiredCommandPool> command_pools{};
};

} // namespace vr::render


