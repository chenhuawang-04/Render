#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace vr::render {

template<typename T>
using FrameMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

template<typename SwapchainHostT>
concept SwapchainBridge = requires(SwapchainHostT& swapchain_,
                                   VulkanContext& context_,
                                   VkSemaphore semaphore_,
                                   VkFence fence_,
                                   uint32_t image_index_) {
    { swapchain_.ImageCount() } -> std::convertible_to<uint32_t>;
    { swapchain_.AcquireNextImage(context_, semaphore_, fence_) };
    { swapchain_.Present(context_, image_index_, semaphore_) };
};

enum class FrameBeginCode : std::uint8_t {
    Ready = 0,
    RecreateSwapchain = 1
};

struct FrameSlot {
    VkSemaphore acquire_binary = VK_NULL_HANDLE;
    VkSemaphore present_binary = VK_NULL_HANDLE;
    VkFence reuse_fence = VK_NULL_HANDLE;
    std::uint64_t graphics_value = 0U;
    std::uint64_t transfer_value = 0U;
    std::uint64_t compute_value = 0U;
};

struct FrameSubmitWait {
    VkSemaphore semaphore = VK_NULL_HANDLE;
    VkPipelineStageFlags stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
};

struct FrameToken {
    std::uint64_t frame_id = 0U;
    uint32_t frame_index = 0U;
    uint32_t image_index = 0U;
    VkSemaphore acquire_binary = VK_NULL_HANDLE;
    VkSemaphore present_binary = VK_NULL_HANDLE;
    VkFence reuse_fence = VK_NULL_HANDLE;
    std::uint64_t graphics_signal_value = 0U;
    std::uint64_t transfer_wait_value = 0U;
    std::uint64_t compute_wait_value = 0U;
    bool recommend_recreate = false;
};

struct FrameBeginResult {
    FrameBeginCode code = FrameBeginCode::Ready;
    FrameToken token{};
};

template<uint32_t frames_in_flight_v = 2U>
class FrameSyncHost final {
    static_assert(frames_in_flight_v > 0U, "frames_in_flight_v must be >= 1");

public:
    FrameSyncHost() = default;
    ~FrameSyncHost() = default;

    FrameSyncHost(const FrameSyncHost&) = delete;
    FrameSyncHost& operator=(const FrameSyncHost&) = delete;

    FrameSyncHost(FrameSyncHost&&) = delete;
    FrameSyncHost& operator=(FrameSyncHost&&) = delete;

    void Initialize(VulkanContext& context_, uint32_t swapchain_image_count_) {
        if (!context_.IsDeviceInitialized()) {
            throw std::runtime_error("FrameSyncHost::Initialize requires initialized Vulkan device");
        }

        Shutdown(context_);

        const VkDevice device_ = context_.Device();
        VkSemaphoreCreateInfo semaphore_create_info{};
        semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fence_create_info{};
        fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        uint32_t created_count = 0U;
        try {
            for (; created_count < frames_in_flight_v; ++created_count) {
                CheckVk("vkCreateSemaphore(acquire_binary)",
                        vkCreateSemaphore(device_,
                                          &semaphore_create_info,
                                          nullptr,
                                          &slots[created_count].acquire_binary));
                CheckVk("vkCreateSemaphore(present_binary)",
                        vkCreateSemaphore(device_,
                                          &semaphore_create_info,
                                          nullptr,
                                          &slots[created_count].present_binary));
                CheckVk("vkCreateFence(reuse_fence)",
                        vkCreateFence(device_, &fence_create_info, nullptr, &slots[created_count].reuse_fence));
            }
        } catch (...) {
            for (uint32_t i = 0U; i <= created_count && i < frames_in_flight_v; ++i) {
                if (slots[i].acquire_binary != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device_, slots[i].acquire_binary, nullptr);
                    slots[i].acquire_binary = VK_NULL_HANDLE;
                }
                if (slots[i].present_binary != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device_, slots[i].present_binary, nullptr);
                    slots[i].present_binary = VK_NULL_HANDLE;
                }
                if (slots[i].reuse_fence != VK_NULL_HANDLE) {
                    vkDestroyFence(device_, slots[i].reuse_fence, nullptr);
                    slots[i].reuse_fence = VK_NULL_HANDLE;
                }
            }
            throw;
        }

        image_owner_fence.resize(swapchain_image_count_);
        for (auto& owner_fence : image_owner_fence) {
            owner_fence = VK_NULL_HANDLE;
        }

        for (auto& reuse_waited : frame_reuse_waited) {
            reuse_waited = false;
        }
        next_graphics_value = 1U;
        graphics_submitted_value = 0U;
        graphics_completed_value = 0U;
        current_frame = 0U;
        initialized = true;
    }

    void Shutdown(VulkanContext& context_) {
        if (!initialized) {
            image_owner_fence.clear();
            current_frame = 0U;
            return;
        }

        const VkDevice device_ = context_.Device();
        if (device_ != VK_NULL_HANDLE) {
            FrameMcVector<VkFence> fences_to_wait;
            fences_to_wait.reserve(frames_in_flight_v);
            for (const auto& slot : slots) {
                if (slot.reuse_fence != VK_NULL_HANDLE) {
                    fences_to_wait.push_back(slot.reuse_fence);
                }
            }
            if (!fences_to_wait.empty()) {
                CheckVk("vkWaitForFences(all in_flight)",
                        vkWaitForFences(device_,
                                        static_cast<uint32_t>(fences_to_wait.size()),
                                        fences_to_wait.data(),
                                        VK_TRUE,
                                        std::numeric_limits<uint64_t>::max()));
            }

            if (context_.GraphicsQueue() != VK_NULL_HANDLE) {
                CheckVk("vkQueueWaitIdle(graphics)",
                        vkQueueWaitIdle(context_.GraphicsQueue()));
            }
            if (context_.PresentQueue() != VK_NULL_HANDLE &&
                context_.PresentQueue() != context_.GraphicsQueue()) {
                CheckVk("vkQueueWaitIdle(present)",
                        vkQueueWaitIdle(context_.PresentQueue()));
            }

            for (auto& slot : slots) {
                if (slot.acquire_binary != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device_, slot.acquire_binary, nullptr);
                    slot.acquire_binary = VK_NULL_HANDLE;
                }
                if (slot.present_binary != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device_, slot.present_binary, nullptr);
                    slot.present_binary = VK_NULL_HANDLE;
                }
                if (slot.reuse_fence != VK_NULL_HANDLE) {
                    vkDestroyFence(device_, slot.reuse_fence, nullptr);
                    slot.reuse_fence = VK_NULL_HANDLE;
                }
            }
        } else {
            for (auto& slot : slots) {
                slot.acquire_binary = VK_NULL_HANDLE;
                slot.present_binary = VK_NULL_HANDLE;
                slot.reuse_fence = VK_NULL_HANDLE;
            }
        }

        image_owner_fence.clear();
        for (auto& slot : slots) {
            slot.graphics_value = 0U;
            slot.transfer_value = 0U;
            slot.compute_value = 0U;
        }
        for (auto& reuse_waited : frame_reuse_waited) {
            reuse_waited = false;
        }
        graphics_completed_value = graphics_submitted_value;
        current_frame = 0U;
        initialized = false;
    }

    void OnSwapchainRecreated(uint32_t swapchain_image_count_) {
        image_owner_fence.resize(swapchain_image_count_);
        for (auto& owner_fence : image_owner_fence) {
            owner_fence = VK_NULL_HANDLE;
        }
    }

    void PrepareCurrentFrame(VulkanContext& context_) {
        if (!initialized) {
            throw std::runtime_error("FrameSyncHost::PrepareCurrentFrame called before Initialize");
        }
        if (!context_.IsDeviceInitialized()) {
            throw std::runtime_error("FrameSyncHost::PrepareCurrentFrame requires initialized Vulkan device");
        }
        WaitCurrentFrameFenceIfNeeded(context_);
    }

    template<SwapchainBridge SwapchainHostT>
    [[nodiscard]] FrameBeginResult BeginFrame(VulkanContext& context_,
                                              SwapchainHostT& swapchain_,
                                              const std::uint64_t frame_id_ = 0U,
                                              const std::uint64_t transfer_wait_value_ = 0U,
                                              const std::uint64_t compute_wait_value_ = 0U) {
        if (!initialized) {
            throw std::runtime_error("FrameSyncHost::BeginFrame called before Initialize");
        }
        if (!context_.IsDeviceInitialized()) {
            throw std::runtime_error("FrameSyncHost::BeginFrame requires initialized Vulkan device");
        }

        WaitCurrentFrameFenceIfNeeded(context_);

        FrameSlot& slot = slots[current_frame];
        frame_reuse_waited[current_frame] = false;

        const uint32_t swapchain_image_count = swapchain_.ImageCount();
        if (image_owner_fence.size() != swapchain_image_count) {
            OnSwapchainRecreated(swapchain_image_count);
        }

        auto acquire_result = swapchain_.AcquireNextImage(context_,
                                                          slot.acquire_binary,
                                                          VK_NULL_HANDLE);

        FrameBeginResult result{};
        if (acquire_result.result == VK_ERROR_OUT_OF_DATE_KHR) {
            result.code = FrameBeginCode::RecreateSwapchain;
            result.token.frame_index = current_frame;
            result.token.recommend_recreate = true;
            return result;
        }

        if (acquire_result.result != VK_SUCCESS &&
            acquire_result.result != VK_SUBOPTIMAL_KHR) {
            ThrowVk("SwapchainHost::AcquireNextImage", acquire_result.result);
        }

        const uint32_t image_index = acquire_result.image_index;
        if (image_index >= image_owner_fence.size()) {
            throw std::runtime_error("Acquire image_index out of image_owner_fence range");
        }

        VkFence owner_fence = image_owner_fence[image_index];
        if (owner_fence != VK_NULL_HANDLE && owner_fence != slot.reuse_fence) {
            CheckVk("vkWaitForFences(image owner)",
                    vkWaitForFences(context_.Device(),
                                    1U,
                                    &owner_fence,
                                    VK_TRUE,
                                    std::numeric_limits<uint64_t>::max()));
        }
        image_owner_fence[image_index] = slot.reuse_fence;

        result.code = FrameBeginCode::Ready;
        result.token.frame_id = frame_id_;
        result.token.frame_index = current_frame;
        result.token.image_index = image_index;
        result.token.acquire_binary = slot.acquire_binary;
        result.token.present_binary = slot.present_binary;
        result.token.reuse_fence = slot.reuse_fence;
        result.token.graphics_signal_value = next_graphics_value;
        result.token.transfer_wait_value = transfer_wait_value_;
        result.token.compute_wait_value = compute_wait_value_;
        ++next_graphics_value;
        result.token.recommend_recreate =
            (acquire_result.result == VK_SUBOPTIMAL_KHR) || acquire_result.need_recreate;
        return result;
    }

    VkResult Submit(VulkanContext& context_,
                    const FrameToken& token_,
                    VkCommandBuffer command_buffer_,
                    VkPipelineStageFlags wait_stage_mask_ = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT) {
        return Submit(context_,
                      token_,
                      command_buffer_,
                      wait_stage_mask_,
                      nullptr,
                      0U);
    }

    VkResult Submit(VulkanContext& context_,
                    const FrameToken& token_,
                    VkCommandBuffer command_buffer_,
                    VkPipelineStageFlags wait_stage_mask_,
                    const FrameSubmitWait* extra_waits_,
                    uint32_t extra_wait_count_) {
        if (!initialized) {
            throw std::runtime_error("FrameSyncHost::Submit called before Initialize");
        }
        if (command_buffer_ == VK_NULL_HANDLE) {
            throw std::runtime_error("FrameSyncHost::Submit requires valid VkCommandBuffer");
        }
        if (token_.reuse_fence == VK_NULL_HANDLE ||
            token_.acquire_binary == VK_NULL_HANDLE ||
            token_.present_binary == VK_NULL_HANDLE) {
            throw std::runtime_error("FrameSyncHost::Submit received invalid FrameToken synchronization objects");
        }
        if (token_.frame_index >= frames_in_flight_v) {
            throw std::runtime_error("FrameSyncHost::Submit token.frame_index out of range");
        }
        if (token_.graphics_signal_value == 0U) {
            throw std::runtime_error("FrameSyncHost::Submit token.graphics_signal_value must be non-zero");
        }
        constexpr uint32_t kMaxWaitSemaphores = 8U;
        if (extra_wait_count_ > kMaxWaitSemaphores - 1U) {
            throw std::runtime_error("FrameSyncHost::Submit extra_wait_count exceeds internal capacity");
        }
        if (extra_wait_count_ > 0U && extra_waits_ == nullptr) {
            throw std::runtime_error("FrameSyncHost::Submit extra_waits must be non-null when count > 0");
        }

        const VkDevice device_ = context_.Device();
        CheckVk("vkResetFences(reuse fence)", vkResetFences(device_, 1U, &token_.reuse_fence));

        std::array<VkSemaphore, kMaxWaitSemaphores> wait_semaphores{};
        std::array<VkPipelineStageFlags, kMaxWaitSemaphores> wait_stage_masks{};
        std::array<VkPipelineStageFlags2, kMaxWaitSemaphores> wait_stage_masks2{};
        uint32_t wait_count = 0U;

        wait_semaphores[wait_count] = token_.acquire_binary;
        wait_stage_masks[wait_count] = wait_stage_mask_ == 0U
            ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
            : wait_stage_mask_;
        wait_stage_masks2[wait_count] = static_cast<VkPipelineStageFlags2>(wait_stage_masks[wait_count]);
        if (wait_stage_masks2[wait_count] == 0U) {
            wait_stage_masks2[wait_count] = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        ++wait_count;

        for (uint32_t i = 0U; i < extra_wait_count_; ++i) {
            const FrameSubmitWait& wait_info = extra_waits_[i];
            if (wait_info.semaphore == VK_NULL_HANDLE) {
                continue;
            }
            if (wait_count >= kMaxWaitSemaphores) {
                throw std::runtime_error("FrameSyncHost::Submit wait semaphore count overflow");
            }
            const VkPipelineStageFlags stage_mask = wait_info.stage_mask == 0U
                ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
                : wait_info.stage_mask;
            wait_semaphores[wait_count] = wait_info.semaphore;
            wait_stage_masks[wait_count] = stage_mask;
            wait_stage_masks2[wait_count] = static_cast<VkPipelineStageFlags2>(stage_mask);
            if (wait_stage_masks2[wait_count] == 0U) {
                wait_stage_masks2[wait_count] = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            }
            ++wait_count;
        }

        if (context_.EnabledVulkan13Features().synchronization2 == VK_TRUE) {
            std::array<VkSemaphoreSubmitInfo, kMaxWaitSemaphores> wait_infos{};
            for (uint32_t i = 0U; i < wait_count; ++i) {
                wait_infos[i].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
                wait_infos[i].semaphore = wait_semaphores[i];
                wait_infos[i].value = 0U;
                wait_infos[i].stageMask = wait_stage_masks2[i];
                wait_infos[i].deviceIndex = 0U;
            }

            VkCommandBufferSubmitInfo command_buffer_info{};
            command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            command_buffer_info.commandBuffer = command_buffer_;
            command_buffer_info.deviceMask = 0U;

            VkSemaphoreSubmitInfo signal_info{};
            signal_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signal_info.semaphore = token_.present_binary;
            signal_info.value = 0U;
            signal_info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            signal_info.deviceIndex = 0U;

            VkSubmitInfo2 submit_info2{};
            submit_info2.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submit_info2.flags = 0U;
            submit_info2.waitSemaphoreInfoCount = wait_count;
            submit_info2.pWaitSemaphoreInfos = wait_infos.data();
            submit_info2.commandBufferInfoCount = 1U;
            submit_info2.pCommandBufferInfos = &command_buffer_info;
            submit_info2.signalSemaphoreInfoCount = 1U;
            submit_info2.pSignalSemaphoreInfos = &signal_info;

            const VkResult submit_result = vkQueueSubmit2(context_.GraphicsQueue(),
                                                          1U,
                                                          &submit_info2,
                                                          token_.reuse_fence);
            CheckVk("vkQueueSubmit2(graphics)", submit_result);
            frame_reuse_waited[token_.frame_index] = false;
            slots[token_.frame_index].graphics_value = token_.graphics_signal_value;
            graphics_submitted_value = std::max(graphics_submitted_value, token_.graphics_signal_value);
            return submit_result;
        }

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.waitSemaphoreCount = wait_count;
        submit_info.pWaitSemaphores = wait_semaphores.data();
        submit_info.pWaitDstStageMask = wait_stage_masks.data();
        submit_info.commandBufferCount = 1U;
        submit_info.pCommandBuffers = &command_buffer_;
        submit_info.signalSemaphoreCount = 1U;
        submit_info.pSignalSemaphores = &token_.present_binary;

        const VkResult submit_result = vkQueueSubmit(context_.GraphicsQueue(),
                                                     1U,
                                                     &submit_info,
                                                     token_.reuse_fence);
        CheckVk("vkQueueSubmit(graphics)", submit_result);
        frame_reuse_waited[token_.frame_index] = false;
        slots[token_.frame_index].graphics_value = token_.graphics_signal_value;
        graphics_submitted_value = std::max(graphics_submitted_value, token_.graphics_signal_value);
        return submit_result;
    }

    template<SwapchainBridge SwapchainHostT>
    [[nodiscard]] bool Present(VulkanContext& context_,
                               SwapchainHostT& swapchain_,
                               const FrameToken& token_) {
        auto present_result = swapchain_.Present(context_, token_.image_index, token_.present_binary);
        return present_result.need_recreate || token_.recommend_recreate;
    }

    void AdvanceFrame() noexcept {
        current_frame = (current_frame + 1U) % frames_in_flight_v;
    }

    [[nodiscard]] bool IsInitialized() const noexcept {
        return initialized;
    }

    [[nodiscard]] uint32_t FramesInFlight() const noexcept {
        return frames_in_flight_v;
    }

    [[nodiscard]] uint32_t CurrentFrameIndex() const noexcept {
        return current_frame;
    }

    [[nodiscard]] uint64_t LastSubmittedValue() const noexcept {
        return graphics_submitted_value;
    }

    [[nodiscard]] uint64_t CompletedSubmitValue() const noexcept {
        return graphics_completed_value;
    }

    [[nodiscard]] uint64_t LastGraphicsSubmittedValue() const noexcept {
        return graphics_submitted_value;
    }

    [[nodiscard]] uint64_t CompletedGraphicsValue() const noexcept {
        return graphics_completed_value;
    }

    [[nodiscard]] uint64_t NextGraphicsSignalValue() const noexcept {
        return next_graphics_value;
    }

private:
    [[nodiscard]] static const char* VkResultName(VkResult result_) noexcept {
        switch (result_) {
            case VK_SUCCESS: return "VK_SUCCESS";
            case VK_NOT_READY: return "VK_NOT_READY";
            case VK_TIMEOUT: return "VK_TIMEOUT";
            case VK_EVENT_SET: return "VK_EVENT_SET";
            case VK_EVENT_RESET: return "VK_EVENT_RESET";
            case VK_INCOMPLETE: return "VK_INCOMPLETE";
            case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
            case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
            case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
            case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
            case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
            case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
            case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
            case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
            case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
            case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
            case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
            case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
            case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
            case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
            default: return "VK_ERROR_UNKNOWN";
        }
    }

    static void ThrowVk(const char* stage_, VkResult result_) {
        std::ostringstream oss;
        oss << stage_ << " failed: " << VkResultName(result_) << " (" << static_cast<int>(result_) << ")";
        throw std::runtime_error(oss.str());
    }

    static void CheckVk(const char* stage_, VkResult result_) {
        if (result_ != VK_SUCCESS) {
            ThrowVk(stage_, result_);
        }
    }

    void WaitCurrentFrameFenceIfNeeded(VulkanContext& context_) {
        if (current_frame >= frames_in_flight_v) {
            throw std::runtime_error("FrameSyncHost::WaitCurrentFrameFenceIfNeeded current_frame out of range");
        }
        if (frame_reuse_waited[current_frame]) {
            return;
        }

        const VkDevice device = context_.Device();
        FrameSlot& slot = slots[current_frame];
        CheckVk("vkWaitForFences(current frame)",
                vkWaitForFences(device,
                                1U,
                                &slot.reuse_fence,
                                VK_TRUE,
                                std::numeric_limits<uint64_t>::max()));
        graphics_completed_value = std::max(graphics_completed_value, slot.graphics_value);
        frame_reuse_waited[current_frame] = true;
    }

private:
    std::array<FrameSlot, frames_in_flight_v> slots{};
    FrameMcVector<VkFence> image_owner_fence{};
    std::array<bool, frames_in_flight_v> frame_reuse_waited{};
    uint64_t next_graphics_value = 1U;
    uint64_t graphics_submitted_value = 0U;
    uint64_t graphics_completed_value = 0U;
    uint32_t current_frame = 0U;
    bool initialized = false;
};

} // namespace vr::render
