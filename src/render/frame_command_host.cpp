#include "vr/render/frame_command_host.hpp"

#include <algorithm>
#include <limits>

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

void FrameCommandHost::Initialize(VulkanContext& context_,
                                  const FrameCommandCreateInfo& create_info_) {
    if (!context_.IsDeviceInitialized()) {
        throw std::runtime_error("FrameCommandHost::Initialize requires initialized Vulkan device");
    }

    Shutdown(context_);

    create_info_cache = create_info_;
    if (create_info_cache.frames_in_flight == 0U) {
        create_info_cache.frames_in_flight = 1U;
    }
    if (create_info_cache.initial_primary_per_frame == 0U) {
        create_info_cache.initial_primary_per_frame = 1U;
    }
    if (create_info_cache.primary_growth_chunk == 0U) {
        create_info_cache.primary_growth_chunk = 1U;
    }

    slots.resize(create_info_cache.frames_in_flight);

    const VkDevice device_ = context_.Device();
    const uint32_t queue_family_index = context_.QueueFamilies().graphics.value();

    uint32_t created_count = 0U;
    try {
        for (; created_count < create_info_cache.frames_in_flight; ++created_count) {
            FrameCommandSlot& slot = slots[created_count];

            VkCommandPoolCreateInfo pool_create_info{};
            pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            pool_create_info.flags = create_info_cache.command_pool_flags;
            pool_create_info.queueFamilyIndex = queue_family_index;

            CheckVk("vkCreateCommandPool(frame)",
                    vkCreateCommandPool(device_, &pool_create_info, nullptr, &slot.pool));

            AllocatePrimaryBuffers(context_, slot, create_info_cache.initial_primary_per_frame);
            slot.used_primary_count = 0U;
        }
    } catch (...) {
        for (uint32_t i = 0U; i <= created_count && i < slots.size(); ++i) {
            FrameCommandSlot& slot = slots[i];
            slot.primary_buffers.clear();
            if (slot.pool != VK_NULL_HANDLE) {
                vkDestroyCommandPool(device_, slot.pool, nullptr);
                slot.pool = VK_NULL_HANDLE;
            }
            slot.used_primary_count = 0U;
        }
        slots.clear();
        throw;
    }

    initialized = true;
}

void FrameCommandHost::Shutdown(VulkanContext& context_) {
    if (!initialized && slots.empty()) {
        return;
    }

    const VkDevice device_ = context_.Device();
    if (device_ != VK_NULL_HANDLE) {
        for (auto& slot : slots) {
            slot.primary_buffers.clear();
            slot.used_primary_count = 0U;
            if (slot.pool != VK_NULL_HANDLE) {
                vkDestroyCommandPool(device_, slot.pool, nullptr);
                slot.pool = VK_NULL_HANDLE;
            }
        }
    } else {
        for (auto& slot : slots) {
            slot.pool = VK_NULL_HANDLE;
            slot.primary_buffers.clear();
            slot.used_primary_count = 0U;
        }
    }

    slots.clear();
    initialized = false;
}

void FrameCommandHost::ResetFrame(VulkanContext& context_, uint32_t frame_index_) {
    if (!initialized) {
        throw std::runtime_error("FrameCommandHost::ResetFrame called before Initialize");
    }

    FrameCommandSlot& slot = SlotAt(frame_index_);
    CheckVk("vkResetCommandPool(frame)",
            vkResetCommandPool(context_.Device(), slot.pool, 0U));
    slot.used_primary_count = 0U;
}

VkCommandBuffer FrameCommandHost::AcquirePrimary(VulkanContext& context_, uint32_t frame_index_) {
    if (!initialized) {
        throw std::runtime_error("FrameCommandHost::AcquirePrimary called before Initialize");
    }

    FrameCommandSlot& slot = SlotAt(frame_index_);
    if (slot.used_primary_count >= slot.primary_buffers.size()) {
        AllocatePrimaryBuffers(context_, slot, create_info_cache.primary_growth_chunk);
    }

    VkCommandBuffer command_buffer = slot.primary_buffers[slot.used_primary_count];
    ++slot.used_primary_count;
    return command_buffer;
}

VkCommandBuffer FrameCommandHost::BeginPrimary(VulkanContext& context_,
                                               uint32_t frame_index_,
                                               VkCommandBufferUsageFlags usage_flags_) {
    VkCommandBuffer command_buffer = AcquirePrimary(context_, frame_index_);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = usage_flags_;
    begin_info.pInheritanceInfo = nullptr;

    CheckVk("vkBeginCommandBuffer(primary)",
            vkBeginCommandBuffer(command_buffer, &begin_info));
    return command_buffer;
}

void FrameCommandHost::EndCommandBuffer(VkCommandBuffer command_buffer_) {
    if (command_buffer_ == VK_NULL_HANDLE) {
        throw std::runtime_error("FrameCommandHost::EndCommandBuffer requires valid VkCommandBuffer");
    }
    CheckVk("vkEndCommandBuffer(primary)", vkEndCommandBuffer(command_buffer_));
}

void FrameCommandHost::ReservePrimary(VulkanContext& context_,
                                      uint32_t frame_index_,
                                      uint32_t primary_count_) {
    if (!initialized) {
        throw std::runtime_error("FrameCommandHost::ReservePrimary called before Initialize");
    }

    FrameCommandSlot& slot = SlotAt(frame_index_);
    if (primary_count_ <= slot.primary_buffers.size()) {
        return;
    }

    const uint32_t alloc_count = primary_count_ - static_cast<uint32_t>(slot.primary_buffers.size());
    AllocatePrimaryBuffers(context_, slot, alloc_count);
}

bool FrameCommandHost::IsInitialized() const noexcept {
    return initialized;
}

uint32_t FrameCommandHost::FramesInFlight() const noexcept {
    return static_cast<uint32_t>(slots.size());
}

uint32_t FrameCommandHost::UsedPrimaryCount(uint32_t frame_index_) const {
    return SlotAt(frame_index_).used_primary_count;
}

VkCommandPool FrameCommandHost::CommandPool(uint32_t frame_index_) const {
    return SlotAt(frame_index_).pool;
}

void FrameCommandHost::ThrowVk(const char* stage_, VkResult result_) {
    std::ostringstream oss;
    oss << stage_ << " failed: " << VkResultName(result_) << " (" << static_cast<int>(result_) << ")";
    throw std::runtime_error(oss.str());
}

void FrameCommandHost::CheckVk(const char* stage_, VkResult result_) {
    if (result_ != VK_SUCCESS) {
        ThrowVk(stage_, result_);
    }
}

FrameCommandSlot& FrameCommandHost::SlotAt(uint32_t frame_index_) {
    if (frame_index_ >= slots.size()) {
        throw std::out_of_range("FrameCommandHost frame_index out of range");
    }
    return slots[frame_index_];
}

const FrameCommandSlot& FrameCommandHost::SlotAt(uint32_t frame_index_) const {
    if (frame_index_ >= slots.size()) {
        throw std::out_of_range("FrameCommandHost frame_index out of range");
    }
    return slots[frame_index_];
}

void FrameCommandHost::AllocatePrimaryBuffers(VulkanContext& context_,
                                              FrameCommandSlot& slot_,
                                              uint32_t alloc_count_) {
    if (alloc_count_ == 0U) {
        return;
    }

    if (alloc_count_ > std::numeric_limits<uint32_t>::max() - slot_.primary_buffers.size()) {
        throw std::runtime_error("FrameCommandHost::AllocatePrimaryBuffers overflow");
    }

    CommandMcVector<VkCommandBuffer> new_buffers;
    new_buffers.resize(alloc_count_);

    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = slot_.pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = alloc_count_;
    CheckVk("vkAllocateCommandBuffers(frame primary)",
            vkAllocateCommandBuffers(context_.Device(), &alloc_info, new_buffers.data()));

    slot_.primary_buffers.reserve(slot_.primary_buffers.size() + alloc_count_);
    for (VkCommandBuffer buffer : new_buffers) {
        slot_.primary_buffers.push_back(buffer);
    }
}

} // namespace vr::render
