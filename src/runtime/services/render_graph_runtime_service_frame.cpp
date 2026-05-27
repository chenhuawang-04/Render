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
    auto reset_pending_history_publish =
        [](FrameHistoryState& history_) noexcept {
            history_.pending_publish_slot = invalid_frame_index;
            history_.pending_frame_index = 0U;
            history_.pending_submission_id = {};
        };
    reset_pending_history_publish(frame_color_history);
    reset_pending_history_publish(frame_depth_history);
    reset_pending_history_publish(frame_motion_history);
    ResetPendingReprojectionPublish();
    ResetPendingJitterPublish();
    last_diagnostics = {};
}

void RenderGraphRuntimeService::Shutdown(VulkanContext& device_) noexcept {
    DestroyMultiQueueResources(device_);
}

void RenderGraphRuntimeService::MarkGraphicsSubmissionEnqueued(
    const vr::render::FrameToken& token_) noexcept {
    const auto publish_history =
        [this, &token_](FrameHistoryState& history_) noexcept {
            if (history_.pending_publish_slot != invalid_frame_index &&
                history_.pending_frame_index == token_.frame_index &&
                history_.pending_publish_slot < history_.slots.size()) {
                history_.previous_frame_index =
                    history_.pending_frame_index;
                history_.previous_submission_id =
                    history_.pending_submission_id;
                history_.published_slot =
                    history_.pending_publish_slot;
                history_.write_slot = history_.published_slot ^ 1U;
                history_.last_invalidation_reason =
                    render_graph::FrameHistoryInvalidationReason::none;
            }
            history_.pending_publish_slot = invalid_frame_index;
            history_.pending_frame_index = 0U;
            history_.pending_submission_id = {};
        };
    publish_history(frame_color_history);
    publish_history(frame_depth_history);
    publish_history(frame_motion_history);
    if (frame_reprojection.pending_available &&
        frame_reprojection.pending_frame_index == token_.frame_index) {
        frame_reprojection.previous_available = true;
        frame_reprojection.previous_view_projection =
            frame_reprojection.pending_view_projection;
        frame_reprojection.previous_frame_index =
            frame_reprojection.pending_frame_index;
        frame_reprojection.previous_submission_id =
            frame_reprojection.pending_submission_id;
        frame_reprojection.last_invalidation_reason =
            render_graph::FrameHistoryInvalidationReason::none;
    } else {
        frame_reprojection.previous_available = false;
    }
    ResetPendingReprojectionPublish();
    if (frame_jitter.pending_available &&
        frame_jitter.pending_frame_index == token_.frame_index) {
        frame_jitter.previous_available = true;
        frame_jitter.previous_uv_x = frame_jitter.pending_uv_x;
        frame_jitter.previous_uv_y = frame_jitter.pending_uv_y;
        frame_jitter.previous_frame_index = frame_jitter.pending_frame_index;
        frame_jitter.previous_submission_id = frame_jitter.pending_submission_id;
        frame_jitter.last_invalidation_reason =
            render_graph::FrameHistoryInvalidationReason::none;
    } else {
        frame_jitter.previous_available = false;
    }
    ResetPendingJitterPublish();

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
    frame_color_history = {};
    frame_depth_history = {};
    frame_motion_history = {};
    frame_reprojection = {};
    frame_jitter = {};
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

void RenderGraphRuntimeService::RegisterImportedTexture(
    const render_graph::ResourceHandle logical_,
    const render::RenderTargetHandle render_target_) {
    RegisterDirectImportedTexture(logical_, render_target_);
}

void RenderGraphRuntimeService::RequestFrameColorHistoryReset() noexcept {
    frame_color_history.reset_requested = true;
    frame_color_history.last_invalidation_reason =
        render_graph::FrameHistoryInvalidationReason::reset_requested;
    frame_depth_history.reset_requested = true;
    frame_depth_history.last_invalidation_reason =
        render_graph::FrameHistoryInvalidationReason::reset_requested;
    frame_motion_history.reset_requested = true;
    frame_motion_history.last_invalidation_reason =
        render_graph::FrameHistoryInvalidationReason::reset_requested;
    frame_reprojection.reset_requested = true;
    frame_reprojection.last_invalidation_reason =
        render_graph::FrameHistoryInvalidationReason::reset_requested;
    frame_jitter.reset_requested = true;
    frame_jitter.last_invalidation_reason =
        render_graph::FrameHistoryInvalidationReason::reset_requested;
}

void RenderGraphRuntimeService::QueueFrameMotionHistoryPublish(
    const std::uint64_t frame_index_,
    const render::SceneSubmissionId submission_id_) noexcept {
    frame_motion_history.pending_publish_slot = frame_motion_history.write_slot;
    frame_motion_history.pending_frame_index = frame_index_;
    frame_motion_history.pending_submission_id = submission_id_;
}

} // namespace vr::runtime::services
