#pragma once

#include "vr/render/frame_command_host.hpp"
#include "vr/vulkan_context.hpp"

#include <cstdint>

namespace vr::runtime {

struct CommandFrameStats final {
    std::uint32_t frame_slot_count = 0U;
    std::uint32_t used_primary_count = 0U;
    VkCommandPool command_pool = VK_NULL_HANDLE;
};

class CommandService final {
public:
    CommandService() = default;

    CommandService(VulkanContext& device_,
                   vr::render::FrameCommandHost& host_) noexcept
        : device(&device_),
          host(&host_) {}

    void Bind(VulkanContext& device_,
              vr::render::FrameCommandHost& host_) noexcept {
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

    void BeginFrame(const std::uint32_t frame_index_) {
        if (device == nullptr || host == nullptr) {
            return;
        }
        host->ResetFrame(*device, frame_index_);
    }

    [[nodiscard]] VkCommandBuffer BeginPrimaryGraphics(
        const std::uint32_t frame_index_,
        const VkCommandBufferUsageFlags usage_) {
        if (device == nullptr || host == nullptr) {
            return VK_NULL_HANDLE;
        }
        return host->BeginPrimary(*device, frame_index_, usage_);
    }

    void End(const VkCommandBuffer command_buffer_) {
        if (host == nullptr || command_buffer_ == VK_NULL_HANDLE) {
            return;
        }
        host->EndCommandBuffer(command_buffer_);
    }

    [[nodiscard]] CommandFrameStats Stats(const std::uint32_t frame_index_) const noexcept {
        if (host == nullptr) {
            return {};
        }
        return {
            .frame_slot_count = host->FramesInFlight(),
            .used_primary_count = host->UsedPrimaryCount(frame_index_),
            .command_pool = host->CommandPool(frame_index_),
        };
    }

private:
    VulkanContext* device = nullptr;
    vr::render::FrameCommandHost* host = nullptr;
};

} // namespace vr::runtime

