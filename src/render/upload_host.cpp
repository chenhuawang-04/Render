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

} // namespace

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

    synchronization2_enabled = context_.EnabledVulkan13Features().synchronization2 == VK_TRUE;
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

    memory_host = nullptr;
    submit_queue = VK_NULL_HANDLE;
    queue_family_index = 0U;
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

    slot.write_head = 0U;
    slot.recording_active = true;
    slot.recorded_work = false;
    slot.stats = {};
    slot.stats.capacity_bytes = create_info_cache.staging_buffer_size;
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

    const VkDeviceSize safe_alignment = std::max<VkDeviceSize>(1U, alignment_);
    const VkDeviceSize aligned_offset = AlignUp(slot.write_head, safe_alignment);
    if (aligned_offset > create_info_cache.staging_buffer_size ||
        size_ > create_info_cache.staging_buffer_size - aligned_offset) {
        throw std::runtime_error("UploadHost staging ring exhausted; increase staging_buffer_size");
    }

    UploadAllocation allocation{};
    allocation.mapped_data = static_cast<void*>(static_cast<char*>(slot.mapped_ptr) + aligned_offset);
    allocation.staging_buffer = slot.staging_buffer;
    allocation.staging_offset = aligned_offset;
    allocation.size = size_;

    slot.write_head = aligned_offset + size_;
    slot.stats.used_bytes = std::max(slot.stats.used_bytes, slot.write_head);
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

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.memoryBarrierCount = 1U;
    dep.pMemoryBarriers = &barrier_;
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

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.memoryBarrierCount = 0U;
    dep.pMemoryBarriers = nullptr;
    dep.bufferMemoryBarrierCount = 1U;
    dep.pBufferMemoryBarriers = &barrier_;
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

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.memoryBarrierCount = 0U;
    dep.pMemoryBarriers = nullptr;
    dep.bufferMemoryBarrierCount = 0U;
    dep.pBufferMemoryBarriers = nullptr;
    dep.imageMemoryBarrierCount = 1U;
    dep.pImageMemoryBarriers = &barrier_;

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

    FlushAllocationIfNeeded(context_, slot, 0U, slot.write_head);
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

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = (submit_info_.wait_semaphore != VK_NULL_HANDLE) ? 1U : 0U;
    submit.pWaitSemaphores = (submit_info_.wait_semaphore != VK_NULL_HANDLE)
        ? &submit_info_.wait_semaphore
        : nullptr;
    submit.pWaitDstStageMask = (submit.waitSemaphoreCount > 0U)
        ? &submit_info_.wait_stage_mask
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

VkQueue UploadHost::SubmitQueue() const noexcept {
    return submit_queue;
}

const UploadFrameStats& UploadHost::FrameStats(uint32_t frame_index_) const {
    return SlotAt(frame_index_).stats;
}

VkDeviceSize UploadHost::CapacityBytes() const noexcept {
    return create_info_cache.staging_buffer_size;
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

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = create_info_cache.staging_buffer_size;
    buffer_info.usage = create_info_cache.staging_buffer_usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    buffer_info.queueFamilyIndexCount = 0U;
    buffer_info.pQueueFamilyIndices = nullptr;
    CheckVk("vkCreateBuffer(upload staging)",
            vkCreateBuffer(device, &buffer_info, nullptr, &slot_.staging_buffer));

    try {
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, slot_.staging_buffer, &requirements);

        slot_.allocation_slice = memory_host->AllocateAndBindBuffer(
            slot_.staging_buffer,
            requirements,
            create_info_cache.staging_memory_properties,
            create_info_cache.staging_memory_properties,
            true,
            Center::Memory::Vulkan::LifetimeHint::frame_local,
            Center::Memory::Vulkan::HostAccess::sequential_write,
            false,
            false);
        slot_.mapped_ptr = slot_.allocation_slice.mapped_ptr;
        if (slot_.mapped_ptr == nullptr) {
            throw std::runtime_error("UploadHost::CreateSlotResources staging allocation is not mapped");
        }

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

    slot_.write_head = 0U;
    slot_.recording_active = false;
    slot_.recorded_work = false;
    slot_.stats = {};
    slot_.stats.capacity_bytes = create_info_cache.staging_buffer_size;
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

        if (slot_.staging_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, slot_.staging_buffer, nullptr);
            slot_.staging_buffer = VK_NULL_HANDLE;
        }
    }

    if (memory_host != nullptr && slot_.allocation_slice.valid()) {
        memory_host->Deallocate(slot_.allocation_slice);
    }

    slot_.allocation_slice = {};
    slot_.mapped_ptr = nullptr;
    slot_.command_buffer = VK_NULL_HANDLE;
    slot_.write_head = 0U;
    slot_.recording_active = false;
    slot_.recorded_work = false;
    slot_.stats = {};
}

void UploadHost::FlushAllocationIfNeeded(VulkanContext& context_,
                                         const UploadFrameSlot& slot_,
                                         VkDeviceSize offset_,
                                         VkDeviceSize size_) const {
    (void)context_;
    if (size_ == 0U) {
        return;
    }
    if (memory_host == nullptr || !slot_.allocation_slice.valid()) {
        throw std::runtime_error("UploadHost::FlushAllocationIfNeeded invalid MemoryCenter staging slice");
    }
    if (!memory_host->FlushSlice(slot_.allocation_slice, offset_, size_)) {
        throw std::runtime_error("UploadHost::FlushAllocationIfNeeded failed in MemoryCenter path");
    }
}

} // namespace vr::render
