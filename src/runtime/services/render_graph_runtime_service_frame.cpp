#include "vr/runtime/services/render_graph_runtime_service.hpp"

#include <algorithm>

namespace vr::runtime::services {

void RenderGraphRuntimeService::BeginFrame(const std::uint32_t frame_index_) noexcept {
    frame_index = frame_index_;
    builder.Reset();
    compiled_graph = {};
    lowered_vulkan_barriers = {};
    command_ready_vulkan_barriers = {};
    record_stats = {};
    prepared_multi_queue_submission = {};
    graph_build_callback_2d = {};
    graph_build_callback_3d = {};
    direct_graph_build_callback = {};
    has_compiled_graph = false;
    queue_execution_policy = {};
    frame_snapshot = std::monostate{};
    direct_imported_textures.clear();
    direct_imported_buffers.clear();
    external_queue_waits.clear();
    last_diagnostics = {};
}

void RenderGraphRuntimeService::Shutdown(VulkanContext& device_) noexcept {
    DestroyMultiQueueResources(device_);
}

void RenderGraphRuntimeService::MarkGraphicsSubmissionEnqueued(
    const vr::render::FrameToken& token_) noexcept {
    if (!prepared_multi_queue_submission.active ||
        !prepared_multi_queue_submission.submitted ||
        token_.frame_index >= multi_queue_frame_slots.size()) {
        prepared_multi_queue_submission = {};
        return;
    }

    auto& slot = multi_queue_frame_slots[token_.frame_index];
    slot.pending_transfer_value =
        (std::max)(slot.pending_transfer_value,
                   prepared_multi_queue_submission.transfer_submitted_value);
    slot.pending_compute_value =
        (std::max)(slot.pending_compute_value,
                   prepared_multi_queue_submission.compute_submitted_value);
    slot.completion_graphics_value = token_.graphics_signal_value;
    slot.pending_completion =
        slot.pending_transfer_value != 0U ||
        slot.pending_compute_value != 0U;
    prepared_multi_queue_submission = {};
}

void RenderGraphRuntimeService::BeginFrameMultiQueue(VulkanContext& device_,
                                                     const std::uint32_t frame_index_,
                                                     const std::uint64_t graphics_completed_) {
    prepared_multi_queue_submission = {};
    if (frame_index_ == invalid_frame_index) {
        return;
    }

    EnsureMultiQueueFrameSlotCapacity(frame_index_ + 1U);
    MultiQueueFrameSlot& slot = multi_queue_frame_slots[frame_index_];
    if (slot.pending_completion &&
        slot.completion_graphics_value != 0U &&
        graphics_completed_ >= slot.completion_graphics_value) {
        completed_transfer_submit_value =
            (std::max)(completed_transfer_submit_value, slot.pending_transfer_value);
        completed_compute_submit_value =
            (std::max)(completed_compute_submit_value, slot.pending_compute_value);
        slot.pending_transfer_value = 0U;
        slot.pending_compute_value = 0U;
        slot.completion_graphics_value = 0U;
        slot.pending_completion = false;
    }

    ResetQueueCommandResources(device_, slot.graphics);
    ResetQueueCommandResources(device_, slot.transfer);
    ResetQueueCommandResources(device_, slot.compute);
}

void RenderGraphRuntimeService::DestroyMultiQueueResources(VulkanContext& device_) noexcept {
    const VkDevice vk_device = device_.Device();
    for (auto& slot : multi_queue_frame_slots) {
        DestroyQueueCommandResources(vk_device, slot.graphics);
        DestroyQueueCommandResources(vk_device, slot.transfer);
        DestroyQueueCommandResources(vk_device, slot.compute);
        for (auto& dependency_ : slot.dependency_semaphores) {
            if (vk_device != VK_NULL_HANDLE && dependency_.semaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(vk_device, dependency_.semaphore, nullptr);
            }
            dependency_.semaphore = VK_NULL_HANDLE;
        }
        slot.dependency_semaphores.clear();
        slot.pending_transfer_value = 0U;
        slot.pending_compute_value = 0U;
        slot.completion_graphics_value = 0U;
        slot.pending_completion = false;
    }
    multi_queue_frame_slots.clear();
    prepared_multi_queue_submission = {};
    next_transfer_submit_value = 1U;
    last_transfer_submitted_value = 0U;
    completed_transfer_submit_value = 0U;
    next_compute_submit_value = 1U;
    last_compute_submitted_value = 0U;
    completed_compute_submit_value = 0U;
}

void RenderGraphRuntimeService::DestroyQueueCommandResources(
    const VkDevice device_,
    QueueCommandResources& resources_) noexcept {
    resources_.primary_buffers.clear();
    resources_.used_primary_count = 0U;
    resources_.queue_family_index = VK_QUEUE_FAMILY_IGNORED;
    if (device_ != VK_NULL_HANDLE && resources_.pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, resources_.pool, nullptr);
    }
    resources_.pool = VK_NULL_HANDLE;
}

void RenderGraphRuntimeService::ResetQueueCommandResources(VulkanContext& device_,
                                                           QueueCommandResources& resources_) {
    if (resources_.pool == VK_NULL_HANDLE) {
        resources_.used_primary_count = 0U;
        return;
    }
    const VkResult reset_result =
        vkResetCommandPool(device_.Device(), resources_.pool, 0U);
    if (reset_result != VK_SUCCESS) {
        throw std::runtime_error(
            "RenderGraphRuntimeService failed to reset a multi-queue command pool");
    }
    resources_.used_primary_count = 0U;
}

} // namespace vr::runtime::services
