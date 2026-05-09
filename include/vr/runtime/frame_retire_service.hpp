#pragma once

#include "vr/render/frame_retire_host.hpp"
#include "vr/vulkan_context.hpp"

#include <limits>

namespace vr::runtime {

class FrameRetireService final {
public:
    FrameRetireService() = default;

    FrameRetireService(VulkanContext& device_,
                       vr::render::FrameRetireHost& host_) noexcept
        : device(&device_),
          host(&host_) {}

    void Bind(VulkanContext& device_,
              vr::render::FrameRetireHost& host_) noexcept {
        device = &device_;
        host = &host_;
    }

    void Reset() noexcept {
        device = nullptr;
        host = nullptr;
    }

    [[nodiscard]] bool IsBound() const noexcept {
        return device != nullptr && host != nullptr;
    }

    void RetireImageView(const VkImageView image_view_,
                         const std::uint64_t retire_value_) {
        if (host != nullptr) {
            host->RetireImageView(image_view_, retire_value_);
        }
    }

    void RetireFramebuffer(const VkFramebuffer framebuffer_,
                           const std::uint64_t retire_value_) {
        if (host != nullptr) {
            host->RetireFramebuffer(framebuffer_, retire_value_);
        }
    }

    void RetireSwapchain(const VkSwapchainKHR swapchain_,
                         const std::uint64_t retire_value_) {
        if (host != nullptr) {
            host->RetireSwapchain(swapchain_, retire_value_);
        }
    }

    void RetireCommandPool(const VkCommandPool command_pool_,
                           const std::uint64_t retire_value_) {
        if (host != nullptr) {
            host->RetireCommandPool(command_pool_, retire_value_);
        }
    }

    [[nodiscard]] vr::render::FrameRetireStats Collect(
        const std::uint64_t completed_value_,
        const std::uint32_t max_destroy_per_type_ = std::numeric_limits<std::uint32_t>::max()) {
        if (device == nullptr || host == nullptr) {
            return {};
        }
        return host->Collect(device->Device(), completed_value_, max_destroy_per_type_);
    }

    [[nodiscard]] const vr::render::FrameRetireHost* Host() const noexcept {
        return host;
    }

private:
    VulkanContext* device = nullptr;
    vr::render::FrameRetireHost* host = nullptr;
};

} // namespace vr::runtime
