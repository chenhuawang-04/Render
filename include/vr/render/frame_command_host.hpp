#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/vulkan_context.hpp"

#include <cstdint>
#include <sstream>
#include <stdexcept>

namespace vr::render {

template<typename T>
using CommandMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct FrameCommandCreateInfo {
    uint32_t frames_in_flight = 2U;
    uint32_t initial_primary_per_frame = 2U;
    uint32_t primary_growth_chunk = 2U;
    VkCommandPoolCreateFlags command_pool_flags =
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
};

struct FrameCommandSlot {
    VkCommandPool pool = VK_NULL_HANDLE;
    CommandMcVector<VkCommandBuffer> primary_buffers{};
    uint32_t used_primary_count = 0U;
};

class FrameCommandHost final {
public:
    FrameCommandHost() = default;
    ~FrameCommandHost() = default;

    FrameCommandHost(const FrameCommandHost&) = delete;
    FrameCommandHost& operator=(const FrameCommandHost&) = delete;

    FrameCommandHost(FrameCommandHost&&) = delete;
    FrameCommandHost& operator=(FrameCommandHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    const FrameCommandCreateInfo& create_info_ = {});

    void Shutdown(VulkanContext& context_);

    void ResetFrame(VulkanContext& context_, uint32_t frame_index_);

    [[nodiscard]] VkCommandBuffer AcquirePrimary(VulkanContext& context_, uint32_t frame_index_);

    [[nodiscard]] VkCommandBuffer BeginPrimary(VulkanContext& context_,
                                               uint32_t frame_index_,
                                               VkCommandBufferUsageFlags usage_flags_ = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    void EndCommandBuffer(VkCommandBuffer command_buffer_);

    void ReservePrimary(VulkanContext& context_, uint32_t frame_index_, uint32_t primary_count_);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] uint32_t FramesInFlight() const noexcept;
    [[nodiscard]] uint32_t UsedPrimaryCount(uint32_t frame_index_) const;
    [[nodiscard]] VkCommandPool CommandPool(uint32_t frame_index_) const;

private:
    static void ThrowVk(const char* stage_, VkResult result_);
    static void CheckVk(const char* stage_, VkResult result_);

    FrameCommandSlot& SlotAt(uint32_t frame_index_);
    const FrameCommandSlot& SlotAt(uint32_t frame_index_) const;

    void AllocatePrimaryBuffers(VulkanContext& context_,
                                FrameCommandSlot& slot_,
                                uint32_t alloc_count_);

private:
    CommandMcVector<FrameCommandSlot> slots{};
    FrameCommandCreateInfo create_info_cache{};
    bool initialized = false;
};

} // namespace vr::render
