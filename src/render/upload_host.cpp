#include "vr/render/upload_host.hpp"

#include "vr/resource/gpu_memory_host.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace vr::render {

namespace {

[[nodiscard]] const char* VkResultName(VkResult result_) noexcept {
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

constexpr VkPipelineStageFlags2 k_graphics_only_stage_mask =
    VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT |
    VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT |
    VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT |
    VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
    VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT |
    VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT |
    VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT |
    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
    VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
    VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;

constexpr VkPipelineStageFlags2 k_compute_only_stage_mask =
    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

} // namespace

bool UploadHost::HasUnsupportedStageForQueue(VkPipelineStageFlags2 stage_mask_,
                                             VkQueueFlags queue_flags_) noexcept {
    if (stage_mask_ == 0U || stage_mask_ == VK_PIPELINE_STAGE_2_NONE) {
        return false;
    }

    const bool supports_graphics = (queue_flags_ & VK_QUEUE_GRAPHICS_BIT) != 0U;
    const bool supports_compute = (queue_flags_ & VK_QUEUE_COMPUTE_BIT) != 0U;

    if (!supports_graphics && (stage_mask_ & k_graphics_only_stage_mask) != 0U) {
        return true;
    }
    if (!supports_compute && (stage_mask_ & k_compute_only_stage_mask) != 0U) {
        return true;
    }
    if ((stage_mask_ & VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT) != 0U &&
        !supports_graphics &&
        !supports_compute) {
        return true;
    }
    return false;
}

VkPipelineStageFlags2 UploadHost::SanitizeStageMaskForSubmitQueue(
    VkPipelineStageFlags2 stage_mask_) const noexcept {
    if (!HasUnsupportedStageForQueue(stage_mask_, submit_queue_flags)) {
        return stage_mask_;
    }
    return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
}

VkPipelineStageFlags UploadHost::SanitizeLegacyStageMaskForSubmitQueue(
    VkPipelineStageFlags stage_mask_) const noexcept {
    const VkPipelineStageFlags2 promoted_stage_mask = PromoteLegacyStageMask(stage_mask_);
    const VkPipelineStageFlags2 sanitized_stage_mask =
        SanitizeStageMaskForSubmitQueue(promoted_stage_mask);
    return static_cast<VkPipelineStageFlags>(sanitized_stage_mask);
}

VkAccessFlags2 UploadHost::SanitizeAccessMaskForStage(VkPipelineStageFlags2 stage_mask_,
                                                      VkAccessFlags2 access_mask_) noexcept {
    if (stage_mask_ == 0U || stage_mask_ == VK_PIPELINE_STAGE_2_NONE) {
        return 0U;
    }
    return access_mask_;
}

void UploadHost::Initialize(VulkanContext& context_,
                            resource::GpuMemoryHost& gpu_memory_host_,
                            const UploadHostCreateInfo& create_info_) {
    if (!context_.IsDeviceInitialized()) {
        throw std::runtime_error("UploadHost::Initialize requires initialized Vulkan device");
    }
    if (!gpu_memory_host_.IsInitialized()) {
        throw std::runtime_error("UploadHost::Initialize requires initialized GpuMemoryHost");
    }

    Shutdown(context_);

    create_info_cache = create_info_;
    if (create_info_cache.frames_in_flight == 0U) {
        create_info_cache.frames_in_flight = 1U;
    }
    if (create_info_cache.staging_buffer_size == 0U) {
        create_info_cache.staging_buffer_size = 1U;
    }
    if (create_info_cache.max_staging_page_count == 0U) {
        create_info_cache.max_staging_page_count = 1U;
    }

    const auto& families = context_.QueueFamilies();
    if (create_info_cache.prefer_transfer_queue &&
        families.transfer.has_value() &&
        context_.TransferQueue() != VK_NULL_HANDLE) {
        queue_family_index = families.transfer.value();
        submit_queue = context_.TransferQueue();
    } else if (create_info_cache.fallback_to_graphics_queue &&
               families.graphics.has_value() &&
               context_.GraphicsQueue() != VK_NULL_HANDLE) {
        queue_family_index = families.graphics.value();
        submit_queue = context_.GraphicsQueue();
    } else {
        throw std::runtime_error("UploadHost::Initialize cannot resolve a valid submit queue");
    }

    if (!families.graphics.has_value()) {
        throw std::runtime_error("UploadHost::Initialize requires graphics queue family");
    }
    graphics_queue_family_index = families.graphics.value();
    cross_queue_submit = queue_family_index != graphics_queue_family_index;

    uint32_t queue_family_count = 0U;
    vkGetPhysicalDeviceQueueFamilyProperties(context_.PhysicalDevice(), &queue_family_count, nullptr);
    if (queue_family_count == 0U || queue_family_index >= queue_family_count) {
        throw std::runtime_error("UploadHost::Initialize failed to query queue family properties");
    }
    UploadMcVector<VkQueueFamilyProperties> queue_properties{};
    queue_properties.resize(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(context_.PhysicalDevice(),
                                             &queue_family_count,
                                             queue_properties.data());
    submit_queue_flags = queue_properties[queue_family_index].queueFlags;

    synchronization2_enabled = context_.EnabledVulkan13Features().synchronization2 == VK_TRUE;
    context = &context_;
    memory_host = &gpu_memory_host_;

    slots.resize(create_info_cache.frames_in_flight);

    uint32_t created_count = 0U;
    try {
        for (; created_count < create_info_cache.frames_in_flight; ++created_count) {
            CreateSlotResources(context_, slots[created_count]);
        }
    } catch (...) {
        for (uint32_t i = 0U; i <= created_count && i < slots.size(); ++i) {
            DestroySlotResources(context_, slots[i]);
        }
        slots.clear();
        context = nullptr;
        memory_host = nullptr;
        submit_queue = VK_NULL_HANDLE;
        throw;
    }

    initialized = true;
}

void UploadHost::Shutdown(VulkanContext& context_) {
    if (!initialized && slots.empty()) {
        return;
    }

    if (context_.Device() != VK_NULL_HANDLE && submit_queue != VK_NULL_HANDLE) {
        (void)vkQueueWaitIdle(submit_queue);
    }

    for (auto& slot : slots) {
        DestroySlotResources(context_, slot);
    }
    slots.clear();

    context = nullptr;
    memory_host = nullptr;
    submit_queue = VK_NULL_HANDLE;
    queue_family_index = 0U;
    graphics_queue_family_index = 0U;
    submit_queue_flags = 0U;
    cross_queue_submit = false;
    synchronization2_enabled = false;
    initialized = false;
}

void UploadHost::BeginFrame(VulkanContext& context_, uint32_t frame_index_) {
    if (!initialized) {
        throw std::runtime_error("UploadHost::BeginFrame called before Initialize");
    }

    UploadFrameSlot& slot = SlotAt(frame_index_);
    CheckVk("vkWaitForFences(upload frame)",
            vkWaitForFences(context_.Device(),
                            1U,
                            &slot.in_flight_fence,
                            VK_TRUE,
                            std::numeric_limits<uint64_t>::max()));

    CheckVk("vkResetCommandPool(upload frame)",
            vkResetCommandPool(context_.Device(), slot.command_pool, 0U));

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    begin_info.pInheritanceInfo = nullptr;
    CheckVk("vkBeginCommandBuffer(upload frame)",
            vkBeginCommandBuffer(slot.command_buffer, &begin_info));

    for (auto& page : slot.pages) {
        page.write_head = 0U;
    }
    slot.recording_active = true;
    slot.recorded_work = false;
    slot.stats = {};
    slot.stats.capacity_bytes = SlotCapacityBytes(slot);
    slot.stats.staging_page_count = static_cast<uint32_t>(slot.pages.size());
}

UploadAllocation UploadHost::Allocate(uint32_t frame_index_,
                                      VkDeviceSize size_,
                                      VkDeviceSize alignment_) {
    if (!initialized) {
        throw std::runtime_error("UploadHost::Allocate called before Initialize");
    }
    if (size_ == 0U) {
        throw std::runtime_error("UploadHost::Allocate requires non-zero size");
    }

    UploadFrameSlot& slot = SlotAt(frame_index_);
    if (!slot.recording_active) {
        throw std::runtime_error("UploadHost::Allocate requires BeginFrame before allocation");
    }

    UploadStagingPage& page = AcquireWritablePage(slot, size_, alignment_);
    const VkDeviceSize safe_alignment = std::max<VkDeviceSize>(1U, alignment_);
    const VkDeviceSize previous_head = page.write_head;
    const VkDeviceSize aligned_offset = AlignUp(previous_head, safe_alignment);

    UploadAllocation allocation{};
    allocation.mapped_data = static_cast<void*>(static_cast<char*>(page.mapped_ptr) + aligned_offset);
    allocation.staging_buffer = page.staging_buffer;
    allocation.staging_offset = aligned_offset;
    allocation.size = size_;

    page.write_head = aligned_offset + size_;
    slot.stats.used_bytes += (page.write_head - previous_head);
    return allocation;
}

UploadAllocation UploadHost::Write(uint32_t frame_index_,
                                   const void* src_data_,
                                   VkDeviceSize size_,
                                   VkDeviceSize alignment_) {
    if (src_data_ == nullptr) {
        throw std::runtime_error("UploadHost::Write requires non-null src_data");
    }

    UploadAllocation allocation = Allocate(frame_index_, size_, alignment_);
    std::memcpy(allocation.mapped_data, src_data_, static_cast<std::size_t>(size_));
    return allocation;
}

void UploadHost::RecordCopyBuffer(uint32_t frame_index_,
                                  VkBuffer dst_buffer_,
                                  VkDeviceSize dst_offset_,
                                  const UploadAllocation& allocation_) {
    if (dst_buffer_ == VK_NULL_HANDLE) {
        throw std::runtime_error("UploadHost::RecordCopyBuffer requires valid destination buffer");
    }
    if (allocation_.staging_buffer == VK_NULL_HANDLE || allocation_.size == 0U) {
        throw std::runtime_error("UploadHost::RecordCopyBuffer requires valid UploadAllocation");
    }

    UploadFrameSlot& slot = SlotAt(frame_index_);
    if (!slot.recording_active) {
        throw std::runtime_error("UploadHost::RecordCopyBuffer requires active recording frame");
    }

    VkBufferCopy copy{};
    copy.srcOffset = allocation_.staging_offset;
    copy.dstOffset = dst_offset_;
    copy.size = allocation_.size;
    vkCmdCopyBuffer(slot.command_buffer,
                    allocation_.staging_buffer,
                    dst_buffer_,
                    1U,
                    &copy);
    slot.recorded_work = true;
    ++slot.stats.buffer_copy_count;
}

void UploadHost::StageAndRecordCopyBuffer(uint32_t frame_index_,
                                          VkBuffer dst_buffer_,
                                          VkDeviceSize dst_offset_,
                                          const void* src_data_,
                                          VkDeviceSize size_,
                                          VkDeviceSize alignment_) {
    UploadAllocation allocation = Write(frame_index_, src_data_, size_, alignment_);
    RecordCopyBuffer(frame_index_, dst_buffer_, dst_offset_, allocation);
}

void UploadHost::RecordCopyImage(uint32_t frame_index_,
                                 VkImage dst_image_,
                                 VkImageLayout dst_layout_,
                                 const VkBufferImageCopy& copy_region_,
                                 const UploadAllocation& allocation_) {
    if (dst_image_ == VK_NULL_HANDLE) {
        throw std::runtime_error("UploadHost::RecordCopyImage requires valid destination image");
    }
    if (allocation_.staging_buffer == VK_NULL_HANDLE || allocation_.size == 0U) {
        throw std::runtime_error("UploadHost::RecordCopyImage requires valid UploadAllocation");
    }

    UploadFrameSlot& slot = SlotAt(frame_index_);
    if (!slot.recording_active) {
        throw std::runtime_error("UploadHost::RecordCopyImage requires active recording frame");
    }

    VkBufferImageCopy region = copy_region_;
    region.bufferOffset = allocation_.staging_offset;
    vkCmdCopyBufferToImage(slot.command_buffer,
                           allocation_.staging_buffer,
                           dst_image_,
                           dst_layout_,
                           1U,
                           &region);
    slot.recorded_work = true;
    ++slot.stats.image_copy_count;
}

void UploadHost::StageAndRecordCopyImage(uint32_t frame_index_,
                                         VkImage dst_image_,
                                         VkImageLayout dst_layout_,
                                         const VkBufferImageCopy& copy_region_,
                                         const void* src_data_,
                                         VkDeviceSize size_,
                                         VkDeviceSize alignment_) {
    UploadAllocation allocation = Write(frame_index_, src_data_, size_, alignment_);
    RecordCopyImage(frame_index_, dst_image_, dst_layout_, copy_region_, allocation);
}

void UploadHost::RecordMemoryBarrier2(uint32_t frame_index_, const VkMemoryBarrier2& barrier_) {
    if (!synchronization2_enabled) {
        throw std::runtime_error(
            "UploadHost::RecordMemoryBarrier2 requires Vulkan13 synchronization2 feature enabled");
    }

    UploadFrameSlot& slot = SlotAt(frame_index_);
    if (!slot.recording_active) {
        throw std::runtime_error("UploadHost::RecordMemoryBarrier2 requires active recording frame");
    }

    VkMemoryBarrier2 barrier = barrier_;
    barrier.srcStageMask = SanitizeStageMaskForSubmitQueue(barrier.srcStageMask);
    barrier.dstStageMask = SanitizeStageMaskForSubmitQueue(barrier.dstStageMask);
    barrier.srcAccessMask = SanitizeAccessMaskForStage(barrier.srcStageMask, barrier.srcAccessMask);
    barrier.dstAccessMask = SanitizeAccessMaskForStage(barrier.dstStageMask, barrier.dstAccessMask);

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.memoryBarrierCount = 1U;
    dep.pMemoryBarriers = &barrier;
    dep.bufferMemoryBarrierCount = 0U;
    dep.pBufferMemoryBarriers = nullptr;
    dep.imageMemoryBarrierCount = 0U;
    dep.pImageMemoryBarriers = nullptr;

    vkCmdPipelineBarrier2(slot.command_buffer, &dep);
    slot.recorded_work = true;
    ++slot.stats.barrier_count;
}

void UploadHost::RecordBufferBarrier2(uint32_t frame_index_, const VkBufferMemoryBarrier2& barrier_) {
    if (!synchronization2_enabled) {
        throw std::runtime_error(
            "UploadHost::RecordBufferBarrier2 requires Vulkan13 synchronization2 feature enabled");
    }

    UploadFrameSlot& slot = SlotAt(frame_index_);
    if (!slot.recording_active) {
        throw std::runtime_error("UploadHost::RecordBufferBarrier2 requires active recording frame");
    }

    VkBufferMemoryBarrier2 barrier = barrier_;
    barrier.srcStageMask = SanitizeStageMaskForSubmitQueue(barrier.srcStageMask);
    barrier.dstStageMask = SanitizeStageMaskForSubmitQueue(barrier.dstStageMask);
    barrier.srcAccessMask = SanitizeAccessMaskForStage(barrier.srcStageMask, barrier.srcAccessMask);
    barrier.dstAccessMask = SanitizeAccessMaskForStage(barrier.dstStageMask, barrier.dstAccessMask);

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.memoryBarrierCount = 0U;
    dep.pMemoryBarriers = nullptr;
    dep.bufferMemoryBarrierCount = 1U;
    dep.pBufferMemoryBarriers = &barrier;
    dep.imageMemoryBarrierCount = 0U;
    dep.pImageMemoryBarriers = nullptr;

    vkCmdPipelineBarrier2(slot.command_buffer, &dep);
    slot.recorded_work = true;
    ++slot.stats.barrier_count;
}

void UploadHost::RecordImageBarrier2(uint32_t frame_index_, const VkImageMemoryBarrier2& barrier_) {
    if (!synchronization2_enabled) {
        throw std::runtime_error(
            "UploadHost::RecordImageBarrier2 requires Vulkan13 synchronization2 feature enabled");
    }

    UploadFrameSlot& slot = SlotAt(frame_index_);
    if (!slot.recording_active) {
        throw std::runtime_error("UploadHost::RecordImageBarrier2 requires active recording frame");
    }

    VkImageMemoryBarrier2 barrier = barrier_;
    barrier.srcStageMask = SanitizeStageMaskForSubmitQueue(barrier.srcStageMask);
    barrier.dstStageMask = SanitizeStageMaskForSubmitQueue(barrier.dstStageMask);
    barrier.srcAccessMask = SanitizeAccessMaskForStage(barrier.srcStageMask, barrier.srcAccessMask);
    barrier.dstAccessMask = SanitizeAccessMaskForStage(barrier.dstStageMask, barrier.dstAccessMask);

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.memoryBarrierCount = 0U;
    dep.pMemoryBarriers = nullptr;
    dep.bufferMemoryBarrierCount = 0U;
    dep.pBufferMemoryBarriers = nullptr;
    dep.imageMemoryBarrierCount = 1U;
    dep.pImageMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(slot.command_buffer, &dep);
    slot.recorded_work = true;
    ++slot.stats.barrier_count;
}

UploadEndFrameResult UploadHost::EndFrameAndSubmit(VulkanContext& context_,
                                                   uint32_t frame_index_,
                                                   const UploadSubmitInfo& submit_info_) {
    if (!initialized) {
        throw std::runtime_error("UploadHost::EndFrameAndSubmit called before Initialize");
    }

    UploadFrameSlot& slot = SlotAt(frame_index_);
    if (!slot.recording_active) {
        throw std::runtime_error("UploadHost::EndFrameAndSubmit requires active recording frame");
    }

    for (const auto& page : slot.pages) {
        FlushAllocationIfNeeded(context_, page, 0U, page.write_head);
    }
    CheckVk("vkEndCommandBuffer(upload frame)", vkEndCommandBuffer(slot.command_buffer));
    slot.recording_active = false;

    const bool needs_submit =
        slot.recorded_work ||
        submit_info_.wait_semaphore != VK_NULL_HANDLE ||
        submit_info_.signal_semaphore != VK_NULL_HANDLE;

    if (!needs_submit) {
        return {
            .result = VK_SUCCESS,
            .submitted = false
        };
    }

    CheckVk("vkResetFences(upload frame)", vkResetFences(context_.Device(), 1U, &slot.in_flight_fence));

    const VkPipelineStageFlags wait_stage_mask = submit_info_.wait_stage_mask == 0U
        ? VK_PIPELINE_STAGE_TRANSFER_BIT
        : submit_info_.wait_stage_mask;
    const VkPipelineStageFlags sanitized_wait_stage_mask =
        SanitizeLegacyStageMaskForSubmitQueue(wait_stage_mask);
    VkPipelineStageFlags2 sanitized_wait_stage_mask2 =
        SanitizeStageMaskForSubmitQueue(PromoteLegacyStageMask(wait_stage_mask));
    if (sanitized_wait_stage_mask2 == 0U || sanitized_wait_stage_mask2 == VK_PIPELINE_STAGE_2_NONE) {
        sanitized_wait_stage_mask2 = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    }

    if (synchronization2_enabled) {
        VkSemaphoreSubmitInfo wait_info{};
        if (submit_info_.wait_semaphore != VK_NULL_HANDLE) {
            wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            wait_info.semaphore = submit_info_.wait_semaphore;
            wait_info.value = 0U;
            wait_info.stageMask = sanitized_wait_stage_mask2;
            wait_info.deviceIndex = 0U;
        }

        VkCommandBufferSubmitInfo command_buffer_info{};
        command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        command_buffer_info.commandBuffer = slot.command_buffer;
        command_buffer_info.deviceMask = 0U;

        VkSemaphoreSubmitInfo signal_info{};
        if (submit_info_.signal_semaphore != VK_NULL_HANDLE) {
            signal_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signal_info.semaphore = submit_info_.signal_semaphore;
            signal_info.value = 0U;
            signal_info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            signal_info.deviceIndex = 0U;
        }

        VkSubmitInfo2 submit2{};
        submit2.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submit2.flags = 0U;
        submit2.waitSemaphoreInfoCount = (submit_info_.wait_semaphore != VK_NULL_HANDLE) ? 1U : 0U;
        submit2.pWaitSemaphoreInfos = (submit2.waitSemaphoreInfoCount > 0U) ? &wait_info : nullptr;
        submit2.commandBufferInfoCount = 1U;
        submit2.pCommandBufferInfos = &command_buffer_info;
        submit2.signalSemaphoreInfoCount = (submit_info_.signal_semaphore != VK_NULL_HANDLE) ? 1U : 0U;
        submit2.pSignalSemaphoreInfos = (submit2.signalSemaphoreInfoCount > 0U) ? &signal_info : nullptr;

        const VkResult submit_result = vkQueueSubmit2(submit_queue, 1U, &submit2, slot.in_flight_fence);
        CheckVk("vkQueueSubmit2(upload frame)", submit_result);
        return {
            .result = submit_result,
            .submitted = true
        };
    }

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = (submit_info_.wait_semaphore != VK_NULL_HANDLE) ? 1U : 0U;
    submit.pWaitSemaphores = (submit_info_.wait_semaphore != VK_NULL_HANDLE)
        ? &submit_info_.wait_semaphore
        : nullptr;
    submit.pWaitDstStageMask = (submit.waitSemaphoreCount > 0U)
        ? &sanitized_wait_stage_mask
        : nullptr;
    submit.commandBufferCount = 1U;
    submit.pCommandBuffers = &slot.command_buffer;
    submit.signalSemaphoreCount = (submit_info_.signal_semaphore != VK_NULL_HANDLE) ? 1U : 0U;
    submit.pSignalSemaphores = (submit_info_.signal_semaphore != VK_NULL_HANDLE)
        ? &submit_info_.signal_semaphore
        : nullptr;

    const VkResult submit_result = vkQueueSubmit(submit_queue, 1U, &submit, slot.in_flight_fence);
    CheckVk("vkQueueSubmit(upload frame)", submit_result);
    return {
        .result = submit_result,
        .submitted = true
    };
}

void UploadHost::WaitFrame(VulkanContext& context_,
                           uint32_t frame_index_,
                           uint64_t timeout_ns_) {
    if (!initialized) {
        throw std::runtime_error("UploadHost::WaitFrame called before Initialize");
    }

    const UploadFrameSlot& slot = SlotAt(frame_index_);
    CheckVk("vkWaitForFences(upload frame)",
            vkWaitForFences(context_.Device(),
                            1U,
                            &slot.in_flight_fence,
                            VK_TRUE,
                            timeout_ns_));
}

void UploadHost::WaitIdle(VulkanContext& context_) {
    if (!initialized || submit_queue == VK_NULL_HANDLE || context_.Device() == VK_NULL_HANDLE) {
        return;
    }
    CheckVk("vkQueueWaitIdle(upload)", vkQueueWaitIdle(submit_queue));
}

bool UploadHost::IsInitialized() const noexcept {
    return initialized;
}

uint32_t UploadHost::FramesInFlight() const noexcept {
    return static_cast<uint32_t>(slots.size());
}

uint32_t UploadHost::QueueFamilyIndex() const noexcept {
    return queue_family_index;
}

uint32_t UploadHost::GraphicsQueueFamilyIndex() const noexcept {
    return graphics_queue_family_index;
}

VkQueue UploadHost::SubmitQueue() const noexcept {
    return submit_queue;
}

VkQueueFlags UploadHost::SubmitQueueFlags() const noexcept {
    return submit_queue_flags;
}

bool UploadHost::UsesCrossQueueSubmit() const noexcept {
    return cross_queue_submit;
}

const UploadFrameStats& UploadHost::FrameStats(uint32_t frame_index_) const {
    return SlotAt(frame_index_).stats;
}

VkDeviceSize UploadHost::CapacityBytes() const noexcept {
    if (slots.empty()) {
        return 0U;
    }
    return SlotCapacityBytes(slots.front());
}

void UploadHost::ThrowVk(const char* stage_, VkResult result_) {
    std::ostringstream oss;
    oss << stage_ << " failed: " << VkResultName(result_) << " (" << static_cast<int>(result_) << ")";
    throw std::runtime_error(oss.str());
}

void UploadHost::CheckVk(const char* stage_, VkResult result_) {
    if (result_ != VK_SUCCESS) {
        ThrowVk(stage_, result_);
    }
}

VkDeviceSize UploadHost::AlignUp(VkDeviceSize value_, VkDeviceSize alignment_) {
    const VkDeviceSize safe_alignment = std::max<VkDeviceSize>(1U, alignment_);
    const VkDeviceSize remainder = value_ % safe_alignment;
    if (remainder == 0U) {
        return value_;
    }
    const VkDeviceSize padding = safe_alignment - remainder;
    if (value_ > std::numeric_limits<VkDeviceSize>::max() - padding) {
        throw std::runtime_error("UploadHost::AlignUp overflow");
    }
    return value_ + padding;
}

VkDeviceSize UploadHost::NextPow2(VkDeviceSize value_) noexcept {
    if (value_ <= 1U) {
        return 1U;
    }

    VkDeviceSize result = 1U;
    while (result < value_) {
        if (result > (std::numeric_limits<VkDeviceSize>::max() >> 1U)) {
            return std::numeric_limits<VkDeviceSize>::max();
        }
        result <<= 1U;
    }
    return result;
}

VkPipelineStageFlags2 UploadHost::PromoteLegacyStageMask(VkPipelineStageFlags stage_mask_) noexcept {
    if (stage_mask_ == 0U) {
        return VK_PIPELINE_STAGE_2_NONE;
    }
    return static_cast<VkPipelineStageFlags2>(stage_mask_);
}

UploadHost::UploadFrameSlot& UploadHost::SlotAt(uint32_t frame_index_) {
    if (frame_index_ >= slots.size()) {
        throw std::out_of_range("UploadHost frame_index out of range");
    }
    return slots[frame_index_];
}

const UploadHost::UploadFrameSlot& UploadHost::SlotAt(uint32_t frame_index_) const {
    if (frame_index_ >= slots.size()) {
        throw std::out_of_range("UploadHost frame_index out of range");
    }
    return slots[frame_index_];
}

void UploadHost::CreateSlotResources(VulkanContext& context_, UploadFrameSlot& slot_) {
    if (memory_host == nullptr) {
        throw std::runtime_error("UploadHost::CreateSlotResources missing GpuMemoryHost");
    }

    const VkDevice device = context_.Device();
    slot_.pages.clear();
    slot_.pages.reserve(create_info_cache.max_staging_page_count);

    try {
        slot_.pages.resize(1U);
        CreateStagingPage(context_, slot_.pages[0U], create_info_cache.staging_buffer_size);

        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = create_info_cache.command_pool_flags;
        pool_info.queueFamilyIndex = queue_family_index;
        CheckVk("vkCreateCommandPool(upload)",
                vkCreateCommandPool(device, &pool_info, nullptr, &slot_.command_pool));

        VkCommandBufferAllocateInfo cmd_alloc{};
        cmd_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmd_alloc.commandPool = slot_.command_pool;
        cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd_alloc.commandBufferCount = 1U;
        CheckVk("vkAllocateCommandBuffers(upload)",
                vkAllocateCommandBuffers(device, &cmd_alloc, &slot_.command_buffer));

        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        CheckVk("vkCreateFence(upload)",
                vkCreateFence(device, &fence_info, nullptr, &slot_.in_flight_fence));
    } catch (...) {
        DestroySlotResources(context_, slot_);
        throw;
    }

    slot_.recording_active = false;
    slot_.recorded_work = false;
    slot_.stats = {};
    slot_.stats.capacity_bytes = SlotCapacityBytes(slot_);
    slot_.stats.staging_page_count = static_cast<uint32_t>(slot_.pages.size());
}

void UploadHost::DestroySlotResources(VulkanContext& context_, UploadFrameSlot& slot_) {
    const VkDevice device = context_.Device();
    if (device != VK_NULL_HANDLE) {
        if (slot_.in_flight_fence != VK_NULL_HANDLE) {
            vkDestroyFence(device, slot_.in_flight_fence, nullptr);
            slot_.in_flight_fence = VK_NULL_HANDLE;
        }

        if (slot_.command_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, slot_.command_pool, nullptr);
            slot_.command_pool = VK_NULL_HANDLE;
        }

    }

    for (auto& page : slot_.pages) {
        DestroyStagingPage(context_, page);
    }

    slot_.pages.clear();
    slot_.command_buffer = VK_NULL_HANDLE;
    slot_.recording_active = false;
    slot_.recorded_work = false;
    slot_.stats = {};
}

void UploadHost::CreateStagingPage(VulkanContext& context_,
                                   UploadStagingPage& page_,
                                   VkDeviceSize capacity_bytes_) {
    if (memory_host == nullptr) {
        throw std::runtime_error("UploadHost::CreateStagingPage missing GpuMemoryHost");
    }

    const VkDevice device = context_.Device();
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = capacity_bytes_;
    buffer_info.usage = create_info_cache.staging_buffer_usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    buffer_info.queueFamilyIndexCount = 0U;
    buffer_info.pQueueFamilyIndices = nullptr;
    CheckVk("vkCreateBuffer(upload staging page)",
            vkCreateBuffer(device, &buffer_info, nullptr, &page_.staging_buffer));

    try {
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, page_.staging_buffer, &requirements);

        page_.allocation_slice = memory_host->AllocateAndBindBuffer(
            page_.staging_buffer,
            requirements,
            create_info_cache.staging_memory_properties,
            create_info_cache.staging_memory_properties,
            true,
            Center::Memory::Vulkan::LifetimeHint::frame_local,
            Center::Memory::Vulkan::HostAccess::sequential_write,
            false,
            false);
        page_.mapped_ptr = page_.allocation_slice.mapped_ptr;
        if (page_.mapped_ptr == nullptr) {
            throw std::runtime_error("UploadHost::CreateStagingPage allocation is not mapped");
        }
        page_.capacity_bytes = capacity_bytes_;
        page_.write_head = 0U;
    } catch (...) {
        DestroyStagingPage(context_, page_);
        throw;
    }
}

void UploadHost::DestroyStagingPage(VulkanContext& context_, UploadStagingPage& page_) {
    const VkDevice device = context_.Device();
    if (device != VK_NULL_HANDLE && page_.staging_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, page_.staging_buffer, nullptr);
    }
    if (memory_host != nullptr && page_.allocation_slice.valid()) {
        memory_host->Deallocate(page_.allocation_slice);
    }
    page_.staging_buffer = VK_NULL_HANDLE;
    page_.allocation_slice = {};
    page_.mapped_ptr = nullptr;
    page_.capacity_bytes = 0U;
    page_.write_head = 0U;
}

UploadHost::UploadStagingPage& UploadHost::AcquireWritablePage(UploadFrameSlot& slot_,
                                                               VkDeviceSize size_,
                                                               VkDeviceSize alignment_) {
    if (context == nullptr) {
        throw std::runtime_error("UploadHost::AcquireWritablePage missing VulkanContext");
    }
    if (slot_.pages.empty()) {
        throw std::runtime_error("UploadHost::AcquireWritablePage has no staging pages");
    }

    const VkDeviceSize safe_alignment = std::max<VkDeviceSize>(1U, alignment_);
    for (auto& page : slot_.pages) {
        const VkDeviceSize aligned_offset = AlignUp(page.write_head, safe_alignment);
        if (aligned_offset <= page.capacity_bytes &&
            size_ <= page.capacity_bytes - aligned_offset) {
            return page;
        }
    }

    if (!create_info_cache.allow_staging_page_growth ||
        slot_.pages.size() >= create_info_cache.max_staging_page_count) {
        const UploadStagingPage& current_page = slot_.pages.back();
        const VkDeviceSize aligned_offset = AlignUp(current_page.write_head, safe_alignment);
        std::ostringstream oss{};
        oss << "UploadHost staging capacity exhausted; requested="
            << size_
            << " aligned_offset="
            << aligned_offset
            << " current_page_capacity="
            << current_page.capacity_bytes
            << " used_bytes="
            << slot_.stats.used_bytes
            << " total_capacity="
            << SlotCapacityBytes(slot_)
            << " page_count="
            << slot_.pages.size();
        throw std::runtime_error(oss.str());
    }

    UploadStagingPage new_page{};
    const VkDeviceSize new_page_capacity =
        std::max(create_info_cache.staging_buffer_size, NextPow2(size_));
    CreateStagingPage(*context, new_page, new_page_capacity);
    slot_.pages.push_back(new_page);
    slot_.stats.capacity_bytes += new_page_capacity;
    slot_.stats.staging_page_count = static_cast<uint32_t>(slot_.pages.size());
    slot_.stats.staging_page_growth_count += 1U;
    return slot_.pages.back();
}

VkDeviceSize UploadHost::SlotCapacityBytes(const UploadFrameSlot& slot_) const noexcept {
    VkDeviceSize total_capacity = 0U;
    for (const auto& page : slot_.pages) {
        total_capacity += page.capacity_bytes;
    }
    return total_capacity;
}

void UploadHost::FlushAllocationIfNeeded(VulkanContext& context_,
                                         const UploadStagingPage& page_,
                                         VkDeviceSize offset_,
                                         VkDeviceSize size_) const {
    (void)context_;
    if (size_ == 0U) {
        return;
    }
    if (memory_host == nullptr || !page_.allocation_slice.valid()) {
        throw std::runtime_error("UploadHost::FlushAllocationIfNeeded invalid MemoryCenter staging slice");
    }
    if (!memory_host->FlushSlice(page_.allocation_slice, offset_, size_)) {
        throw std::runtime_error("UploadHost::FlushAllocationIfNeeded failed in MemoryCenter path");
    }
}

} // namespace vr::render
